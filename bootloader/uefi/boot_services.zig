//! Retrieves the final UEFI memory map and exits boot services, producing a
//! coalesced kernel-facing memory map (mirrors the C bootloader's
//! `uefi/boot_services.c`, but sorts with `std.mem.sort` instead of a
//! hand-rolled quicksort, and only coalesces descriptors that are actually
//! physically adjacent).
//!
//! Split into `prepareMemoryMap` (boot services still active) and
//! `finishExitAndBuildMap` (calls ExitBootServices) because the caller needs
//! to know the highest physical address in use *before* exiting -- QEMU's
//! virt board, given enough RAM, splits it into a low window and a high
//! window (e.g. observed around 18GB with `-m 16G`), so a bootloader cannot
//! assume its own code, data, or freshly allocated page tables sit below
//! any particular fixed address; the identity map has to cover whatever the
//! real memory map says.

const std = @import("std");
const uefi = std.os.uefi;
const shared = @import("shared_types");

const serial = @import("../logging/serial.zig");

pub const KernelMemoryMap = struct {
    regions: []shared.MemoryRegion,
};

pub const MemoryMapPrep = struct {
    raw_buf: []align(8) u8,
    normalized: []uefi.tables.MemoryDescriptor,
    regions: []shared.MemoryRegion,
    map_slice: uefi.tables.MemoryMapSlice,
    /// Highest (physical_start + size) seen across every descriptor,
    /// regardless of type -- i.e. the top of all physical address space
    /// firmware is using or has told us about.
    max_physical_addr: u64,
};

fn classify(t: uefi.tables.MemoryType) shared.MemoryType {
    return switch (t) {
        // NOT `.loader_code`/`.loader_data`: unlike a typical UEFI loader
        // that hands off to a separate kernel image and can let its own
        // memory be reclaimed, this bootloader *is* the kernel -- it keeps
        // running (and keeps borrowing pointers into what it allocated)
        // long after this map is built. `.loader_code` is this binary's own
        // running .text/.data; `.loader_data` backs the module registry's
        // whole-boot bump arena (`g_bump`) and every module's leaked-on-
        // purpose ELF file buffer, which the registry's driver_type/
        // driver_name/dependency strings are borrowed slices into (see
        // `module_loader.zig`'s `sectionName`) -- still read by
        // `initializeAll` long after `ExitBootServices`. Classifying either
        // as free let the pmm hand those exact pages back out once its
        // allocator saw real use (heap/slab stress tests), silently
        // corrupting live registry strings (e.g. a dependency name reading
        // back empty) and panicking the next module init with
        // `error.InstanceNotFound`.
        .boot_services_code, .boot_services_data, .conventional_memory => .free,
        else => .used,
    };
}

fn descriptorLessThan(_: void, a: uefi.tables.MemoryDescriptor, b: uefi.tables.MemoryDescriptor) bool {
    return a.physical_start < b.physical_start;
}

fn coalesce(sorted: []const uefi.tables.MemoryDescriptor, out: []shared.MemoryRegion) usize {
    var count: usize = 0;
    for (sorted) |desc| {
        const t = classify(desc.type);
        const end_of_prev = if (count > 0) out[count - 1].start + out[count - 1].page_count * shared.PAGE_SIZE else 0;

        if (count > 0 and out[count - 1].memory_type == t and end_of_prev == desc.physical_start) {
            out[count - 1].page_count += desc.number_of_pages;
        } else {
            out[count] = .{ .start = desc.physical_start, .page_count = desc.number_of_pages, .memory_type = t };
            count += 1;
        }
    }
    return count;
}

/// Fetches the memory map and allocates every buffer `finishExitAndBuildMap`
/// will need, all while boot services are still usable. Returns the highest
/// physical address seen, so the caller can size an identity map that
/// actually covers everything before calling ExitBootServices.
pub fn prepareMemoryMap() !MemoryMapPrep {
    const bs = uefi.system_table.boot_services.?;

    const info = bs.getMemoryMapInfo() catch |err| {
        serial.failLog("Getting memory map size");
        return err;
    };

    // Extra slack absorbs the map growing due to our own allocations below.
    const max_entries = info.len + 16;

    const raw_buf = bs.allocatePool(.loader_data, max_entries * info.descriptor_size) catch |err| {
        serial.failLog("Allocated memory map buffer");
        return err;
    };
    serial.okLog("Allocated memory map buffer");

    const normalized_buf = bs.allocatePool(.loader_data, max_entries * @sizeOf(uefi.tables.MemoryDescriptor)) catch |err| {
        serial.failLog("Allocated memory map scratch");
        return err;
    };
    const normalized: []uefi.tables.MemoryDescriptor = std.mem.bytesAsSlice(uefi.tables.MemoryDescriptor, normalized_buf);

    const regions_buf = bs.allocatePool(.loader_data, max_entries * @sizeOf(shared.MemoryRegion)) catch |err| {
        serial.failLog("Allocated kernel memory map");
        return err;
    };
    const regions: []shared.MemoryRegion = std.mem.bytesAsSlice(shared.MemoryRegion, regions_buf);

    const map_slice = bs.getMemoryMap(raw_buf) catch |err| {
        serial.failLog("Getting memory map");
        return err;
    };
    serial.okLog("Getting memory map");

    var max_physical_addr: u64 = 0;
    var it = map_slice.iterator();
    while (it.next()) |desc| {
        const end = desc.physical_start + desc.number_of_pages * shared.PAGE_SIZE;
        if (end > max_physical_addr) max_physical_addr = end;
    }

    return .{
        .raw_buf = raw_buf,
        .normalized = normalized,
        .regions = regions,
        .map_slice = map_slice,
        .max_physical_addr = max_physical_addr,
    };
}

/// Calls ExitBootServices and returns the coalesced kernel-facing map, using
/// the buffers `prepareMemoryMap` already allocated (no further allocation
/// is possible once boot services have exited).
pub fn finishExitAndBuildMap(image_handle: uefi.Handle, prep: *MemoryMapPrep) !KernelMemoryMap {
    const bs = uefi.system_table.boot_services.?;

    bs.exitBootServices(image_handle, prep.map_slice.info.key) catch |err| switch (err) {
        error.InvalidParameter => {
            // The map changed since prepareMemoryMap(); per spec, refresh it
            // once and retry.
            prep.map_slice = try bs.getMemoryMap(prep.raw_buf);
            try bs.exitBootServices(image_handle, prep.map_slice.info.key);
        },
        else => return err,
    };

    // Firmware event/timer servicing is meaningless once boot services have
    // exited, but the ARM generic timer interrupt firmware armed keeps
    // ticking regardless, and VBAR_EL1 still points at *firmware's* vector
    // table. Left unmasked, that IRQ fires into a vector table that our own
    // (much smaller) page tables may not even map, faulting forever. We
    // don't install our own vector table yet, so mask everything.
    asm volatile ("msr daifset, #0xf");

    serial.okLog("Exited boot services");

    var norm_count: usize = 0;
    var it = prep.map_slice.iterator();
    while (it.next()) |desc| {
        prep.normalized[norm_count] = desc.*;
        norm_count += 1;
    }

    std.mem.sort(uefi.tables.MemoryDescriptor, prep.normalized[0..norm_count], {}, descriptorLessThan);

    const region_count = coalesce(prep.normalized[0..norm_count], prep.regions);
    serial.okLog("Built kernel memory map");

    return .{ .regions = prep.regions[0..region_count] };
}

// --- Unit tests -------------------------------------------------------

const testing = std.testing;

fn testDescriptor(memory_type: uefi.tables.MemoryType, start: u64, pages: u64) uefi.tables.MemoryDescriptor {
    return .{
        .type = memory_type,
        .physical_start = start,
        .virtual_start = 0,
        .number_of_pages = pages,
        .attribute = @bitCast(@as(u64, 0)),
    };
}

test "classify groups usable firmware memory as free, everything else (including this binary's own loader memory) as used" {
    try testing.expectEqual(shared.MemoryType.free, classify(.conventional_memory));
    try testing.expectEqual(shared.MemoryType.free, classify(.boot_services_code));
    try testing.expectEqual(shared.MemoryType.free, classify(.boot_services_data));
    // This bootloader is the kernel -- it keeps running out of `.loader_code`
    // and keeps live references into `.loader_data` well past this map
    // being built (see `classify`'s doc comment), so both stay reserved.
    try testing.expectEqual(shared.MemoryType.used, classify(.loader_data));
    try testing.expectEqual(shared.MemoryType.used, classify(.loader_code));
    try testing.expectEqual(shared.MemoryType.used, classify(.reserved_memory_type));
    try testing.expectEqual(shared.MemoryType.used, classify(.runtime_services_code));
    try testing.expectEqual(shared.MemoryType.used, classify(.memory_mapped_io));
}

test "coalesce merges physically adjacent same-type descriptors" {
    const descs = [_]uefi.tables.MemoryDescriptor{
        testDescriptor(.conventional_memory, 0x1000, 1), // [0x1000, 0x2000)
        testDescriptor(.conventional_memory, 0x2000, 1), // [0x2000, 0x3000) -- adjacent, same type
    };
    var out: [4]shared.MemoryRegion = undefined;
    const n = coalesce(&descs, &out);

    try testing.expectEqual(@as(usize, 1), n);
    try testing.expectEqual(@as(u64, 0x1000), out[0].start);
    try testing.expectEqual(@as(u64, 2), out[0].page_count);
    try testing.expectEqual(shared.MemoryType.free, out[0].memory_type);
}

test "coalesce does not merge same-type descriptors across a physical gap" {
    // The C bootloader's original coalescing pass merged any two
    // consecutive same-type entries regardless of whether they were
    // actually adjacent, which could silently claim an undescribed gap as
    // free memory. Two conventional_memory regions with a hole between
    // them must stay separate.
    const descs = [_]uefi.tables.MemoryDescriptor{
        testDescriptor(.conventional_memory, 0x1000, 1), // [0x1000, 0x2000)
        testDescriptor(.conventional_memory, 0x5000, 1), // [0x5000, 0x6000) -- gap before this
    };
    var out: [4]shared.MemoryRegion = undefined;
    const n = coalesce(&descs, &out);

    try testing.expectEqual(@as(usize, 2), n);
    try testing.expectEqual(@as(u64, 0x1000), out[0].start);
    try testing.expectEqual(@as(u64, 0x5000), out[1].start);
}

test "coalesce keeps adjacent descriptors of different types separate" {
    const descs = [_]uefi.tables.MemoryDescriptor{
        testDescriptor(.conventional_memory, 0x1000, 1),
        testDescriptor(.reserved_memory_type, 0x2000, 1),
    };
    var out: [4]shared.MemoryRegion = undefined;
    const n = coalesce(&descs, &out);

    try testing.expectEqual(@as(usize, 2), n);
    try testing.expectEqual(shared.MemoryType.free, out[0].memory_type);
    try testing.expectEqual(shared.MemoryType.used, out[1].memory_type);
}

test "descriptorLessThan orders by physical_start" {
    const a = testDescriptor(.conventional_memory, 0x1000, 1);
    const b = testDescriptor(.conventional_memory, 0x2000, 1);
    try testing.expect(descriptorLessThan({}, a, b));
    try testing.expect(!descriptorLessThan({}, b, a));
}
