//! Parses kernel.ini, and for each enabled module: reads its ELF file,
//! relocates it to a private slot in the high half of the address space,
//! maps its PT_LOAD segments, and registers whatever it exports/imports/
//! tests. Mirrors the C bootloader's modules/{elf_loader,kernel_loader,
//! linker}.c combined into one file, since in this port they share enough
//! state (the running load offset) to not be worth splitting.

const std = @import("std");
const uefi = std.os.uefi;
const shared = @import("shared_types");

const Elf64_Ehdr = std.elf.Elf64_Ehdr;
const Elf64_Phdr = std.elf.Elf64_Phdr;
const Elf64_Shdr = std.elf.Elf64_Shdr;
const Elf64_Sym = std.elf.Elf64_Sym;
const Elf64_Rela = std.elf.Elf64_Rela;

const Filesystem = @import("../uefi/filesystem.zig");
const PageTableMod = @import("../memory/page_table.zig");
const PageTable = PageTableMod.PageTable;
const Registry = @import("module_registry.zig");
const ImportMod = @import("module_import.zig");
const Serial = @import("../logging/serial.zig");

/// Registry category used for modules that export no named interface of
/// their own (pure consumer/test modules). The C bootloader has no
/// equivalent -- there, every loadable module was required to export at
/// least one interface, since that export's (type, name) was also the
/// module's only identity for init/test scheduling. This is a deliberate
/// extension: it lets a module import + test other modules without also
/// needing to export a (possibly meaningless) interface of its own.
const MODULE_CATEGORY_FALLBACK = "module";

fn ptrAt(base: [*]u8, off: u64) [*]u8 {
    return base + @as(usize, @intCast(off));
}

fn elfHeader(buf: []align(8) u8) !*Elf64_Ehdr {
    if (buf.len < @sizeOf(Elf64_Ehdr)) return error.BufferTooSmall;
    const header: *Elf64_Ehdr = @ptrCast(@alignCast(buf.ptr));
    if (!std.mem.eql(u8, header.e_ident[0..4], std.elf.MAGIC)) return error.InvalidElfMagic;
    return header;
}

fn phdrSlice(buf: []align(8) u8, header: *const Elf64_Ehdr) []Elf64_Phdr {
    const ptr: [*]Elf64_Phdr = @ptrCast(@alignCast(ptrAt(buf.ptr, header.e_phoff)));
    return ptr[0..header.e_phnum];
}

fn shdrSlice(buf: []align(8) u8, header: *const Elf64_Ehdr) []Elf64_Shdr {
    const ptr: [*]Elf64_Shdr = @ptrCast(@alignCast(ptrAt(buf.ptr, header.e_shoff)));
    return ptr[0..header.e_shnum];
}

fn sectionName(buf: []align(8) u8, shstrtab: Elf64_Shdr, shdr: Elf64_Shdr) []const u8 {
    const off = shstrtab.sh_offset + @as(u64, shdr.sh_name);
    const name_ptr: [*:0]const u8 = @ptrCast(ptrAt(buf.ptr, off));
    return std.mem.sliceTo(name_ptr, 0);
}

fn stripPrefix(s: []const u8, prefix: []const u8) ?[]const u8 {
    if (!std.mem.startsWith(u8, s, prefix)) return null;
    return s[prefix.len..];
}

pub const ElfSpan = struct {
    /// Page-aligned start of the lowest PT_LOAD segment, in the module's
    /// *own* link-time address space (modules are not guaranteed to link
    /// at 0 -- Zig's default freestanding executable base is wherever the
    /// linker feels like, e.g. 0x1000000).
    start: u64,
    pages: u64,
};

/// Computes the page-aligned virtual address range the module's PT_LOAD
/// segments span, before relocation. Used both to size the running load
/// offset and to compute the correct relocation delta (see `loadElf`):
/// shifting by a flat `target - 0` only places the module correctly if it
/// happens to link at address 0, which is not guaranteed.
fn elfSpan(buf: []align(8) u8) !ElfSpan {
    const header = try elfHeader(buf);
    var min_addr: u64 = std.math.maxInt(u64);
    var max_addr: u64 = 0;
    var found = false;

    for (phdrSlice(buf, header)) |phdr| {
        if (phdr.p_type != std.elf.PT_LOAD) continue;
        const start = phdr.p_vaddr;
        const end = start + phdr.p_memsz;
        if (start < min_addr) min_addr = start;
        if (end > max_addr) max_addr = end;
        found = true;
    }

    if (!found) return .{ .start = 0, .pages = 0 };

    const span_start = std.mem.alignBackward(u64, min_addr, shared.PAGE_SIZE);
    const span_end = std.mem.alignForward(u64, max_addr, shared.PAGE_SIZE);
    return .{ .start = span_start, .pages = (span_end - span_start) / shared.PAGE_SIZE };
}

fn segmentContaining(phdrs: []const Elf64_Phdr, vaddr: u64) ?Elf64_Phdr {
    for (phdrs) |phdr| {
        if (phdr.p_type != std.elf.PT_LOAD) continue;
        if (vaddr >= phdr.p_vaddr and vaddr < phdr.p_vaddr + phdr.p_memsz) return phdr;
    }
    return null;
}

/// Shifts every address-bearing field in the ELF image by `delta`: the
/// entry point, PT_LOAD program header addresses, section addresses, and
/// every `R_AARCH64_RELATIVE` relocation target. Modules build as PIE
/// (position-independent executables) specifically so the linker emits
/// `.rela.dyn` with these relocations instead of baking in absolute
/// addresses -- this is what lets a module be loaded at whatever address
/// the bootloader decides to place it at.
pub fn linkElfModule(buf: []align(8) u8, delta: u64) !void {
    const header = try elfHeader(buf);
    const phdrs = phdrSlice(buf, header);
    const shdrs = shdrSlice(buf, header);

    // Apply relocations first, while phdr.p_vaddr still reflects the
    // module's original (pre-shift) link-time addresses: a RELATIVE
    // relocation's r_offset is a link-time virtual address, and finding
    // which PT_LOAD segment (hence file offset) it falls in requires the
    // unshifted values.
    for (shdrs) |shdr| {
        if (shdr.sh_type != std.elf.SHT_RELA) continue;
        const rela_ptr: [*]Elf64_Rela = @ptrCast(@alignCast(ptrAt(buf.ptr, shdr.sh_offset)));
        const relas = rela_ptr[0 .. shdr.sh_size / @sizeOf(Elf64_Rela)];

        for (relas) |rela| {
            const rela_type: u32 = @truncate(rela.r_info & 0xFFFFFFFF);
            if (rela_type != @intFromEnum(std.elf.R_AARCH64.RELATIVE)) continue;

            const segment = segmentContaining(phdrs, rela.r_offset) orelse return error.RelocationOutsideSegment;
            const file_offset = segment.p_offset + (rela.r_offset - segment.p_vaddr);
            const patch_ptr: *align(1) u64 = @ptrCast(ptrAt(buf.ptr, file_offset));
            patch_ptr.* = delta +% @as(u64, @bitCast(rela.r_addend));
        }
    }

    if (header.e_entry != 0) header.e_entry += delta;

    for (phdrs) |*phdr| {
        if (phdr.p_type != std.elf.PT_LOAD) continue;
        phdr.p_vaddr += delta;
        phdr.p_paddr += delta;
    }

    // Every section's sh_addr gets shifted uniformly; non-allocated
    // sections (debug info etc.) have sh_addr == 0 and nothing ever reads
    // it, so shifting it too is harmless.
    for (shdrs) |*shdr| {
        shdr.sh_addr += delta;
    }
}

const SegmentMapping = struct {
    page_aligned_vaddr: u64,
    offset_in_page: u64,
    page_count: usize,
};

/// A PT_LOAD segment's p_vaddr is only guaranteed congruent to p_offset mod
/// p_align (commonly 64KB) -- it is *not* guaranteed 4KB-page-aligned. The
/// MMU always applies the low 12 bits of a virtual address as an in-page
/// offset no matter what physical frame a PTE names, so a segment's bytes
/// have to start at that same sub-page offset within whatever physical
/// range backs it, and the mapping has to start at the containing page
/// boundary rather than at p_vaddr itself.
fn computeSegmentMapping(phdr: Elf64_Phdr) SegmentMapping {
    const page_aligned_vaddr = std.mem.alignBackward(u64, phdr.p_vaddr, shared.PAGE_SIZE);
    const offset_in_page = phdr.p_vaddr - page_aligned_vaddr;
    const span_bytes = offset_in_page + phdr.p_memsz;
    const page_count: usize = @intCast((span_bytes + shared.PAGE_SIZE - 1) / shared.PAGE_SIZE);
    return .{ .page_aligned_vaddr = page_aligned_vaddr, .offset_in_page = offset_in_page, .page_count = page_count };
}

pub const ModuleLoader = struct {
    load_offset: u64,

    pub fn init(initial_load_offset: u64) ModuleLoader {
        return .{ .load_offset = initial_load_offset };
    }

    fn buildModulePath(buf: []u16, name: []const u8) [:0]const u16 {
        const prefix = std.unicode.utf8ToUtf16LeStringLiteral("\\modules\\");
        const suffix = std.unicode.utf8ToUtf16LeStringLiteral(".ko");
        @memcpy(buf[0..prefix.len], prefix);
        for (name, 0..) |c, i| buf[prefix.len + i] = c;
        @memcpy(buf[prefix.len + name.len ..][0..suffix.len], suffix);
        buf[prefix.len + name.len + suffix.len] = 0;
        return buf[0 .. prefix.len + name.len + suffix.len :0];
    }

    fn loadModule(
        self: *ModuleLoader,
        root: *uefi.protocol.File,
        name: []const u8,
        page_table: *PageTable,
        registry: *Registry.DriverRegistry,
        imports: *ImportMod.ModuleImportHandles,
    ) !void {
        var path_buf: [300]u16 = undefined;
        const path = buildModulePath(&path_buf, name);

        const buf = Filesystem.readFile(path, root) catch |err| {
            Serial.failLog("Reading module elf");
            return err;
        };
        Serial.okLog("Reading module elf");

        try self.loadElf(buf, name, page_table, registry, imports);
    }

    fn loadElf(
        self: *ModuleLoader,
        buf: []align(8) u8,
        module_name: []const u8,
        page_table: *PageTable,
        registry: *Registry.DriverRegistry,
        imports: *ImportMod.ModuleImportHandles,
    ) !void {
        const bs = uefi.system_table.boot_services.?;

        const span = try elfSpan(buf);
        // Shifting by `target - span.start` (rather than just `target`)
        // places the module's lowest PT_LOAD page exactly at `target`
        // regardless of whatever address it happened to link at.
        const target = shared.VIRTUAL_MODULE_LOAD_START + self.load_offset;
        const delta = target - span.start;
        linkElfModule(buf, delta) catch |err| {
            Serial.failLog("Linked module elf");
            return err;
        };
        self.load_offset += span.pages * shared.PAGE_SIZE;

        const header = try elfHeader(buf);
        const phdrs = phdrSlice(buf, header);
        const shdrs = shdrSlice(buf, header);
        const shstrtab = shdrs[header.e_shstrndx];

        // 1. Map every PT_LOAD segment into a freshly allocated physical
        //    range at its (already relocated) virtual address.
        //
        //    p_vaddr is only guaranteed congruent to p_offset mod p_align
        //    (commonly 64KB) -- it is *not* guaranteed 4KB-page-aligned,
        //    and generally isn't for anything past the first segment (e.g.
        //    a `.text` segment has been observed at p_vaddr 0x229d0). The
        //    MMU always applies the low 12 bits of a virtual address as an
        //    in-page offset no matter what physical frame a PTE names, so
        //    the segment's bytes have to start at that same sub-page
        //    offset within the allocated physical range, and the mapping
        //    has to start at the containing page boundary, not at p_vaddr
        //    itself.
        for (phdrs) |phdr| {
            if (phdr.p_type != std.elf.PT_LOAD) continue;

            const m = computeSegmentMapping(phdr);
            const pages = bs.allocatePages(.any, .runtime_services_code, m.page_count) catch |err| {
                Serial.failLog("Allocated kernel load segment");
                return err;
            };
            const seg_phys: [*]u8 = @ptrCast(pages.ptr);
            @memset(seg_phys[0 .. m.page_count * shared.PAGE_SIZE], 0);

            // p_memsz can exceed p_filesz (the remainder is BSS); only the
            // first p_filesz bytes actually exist in the file, the rest
            // stays zeroed from the memset above.
            const src = ptrAt(buf.ptr, phdr.p_offset);
            @memcpy((seg_phys + m.offset_in_page)[0..phdr.p_filesz], src[0..phdr.p_filesz]);

            page_table.mapMemory(m.page_aligned_vaddr, @intFromPtr(seg_phys), .four_kb, m.page_count) catch |err| {
                Serial.failLog("Mapped kernel load segment");
                return err;
            };
        }
        Serial.okLog("Mapped kernel load segments");

        // 2. Find this module's `.kmodule.tests` array, if any.
        var tests: []const shared.TestEntry = &.{};
        for (shdrs) |shdr| {
            if (std.mem.eql(u8, sectionName(buf, shstrtab, shdr), shared.TESTS_SECTION)) {
                const ptr: [*]const shared.TestEntry = @ptrFromInt(shdr.sh_addr);
                tests = ptr[0 .. shdr.sh_size / @sizeOf(shared.TestEntry)];
            }
        }

        // 3. Register every `.kmodule.export.<type>.<name>` interface.
        var owner_type: []const u8 = MODULE_CATEGORY_FALLBACK;
        var owner_name: []const u8 = module_name;
        var found_export = false;

        for (shdrs) |shdr| {
            const name = sectionName(buf, shstrtab, shdr);
            const rest = stripPrefix(name, shared.EXPORT_SECTION_PREFIX) orelse continue;
            const dot = std.mem.indexOfScalar(u8, rest, '.') orelse {
                Serial.failLog("Parsed export section name");
                return error.InvalidExportSection;
            };
            const type_name = rest[0..dot];
            const inst_name = rest[dot + 1 ..];

            registry.put(.{
                .driver_type = type_name,
                .driver_name = inst_name,
                .vtable_ptr = @ptrFromInt(shdr.sh_addr),
                .vtable_size = shdr.sh_size,
                .entry_fn = null,
                .tests = &.{},
            }) catch |err| {
                Serial.failLog("Put module into registry");
                return err;
            };

            owner_type = type_name;
            owner_name = inst_name;
            found_export = true;
        }

        // Attach the entry point + tests to whichever identity owns this
        // module: its last real export (matching the C convention when
        // there is one), or the synthetic "module.<name>" fallback when it
        // exports nothing.
        const entry_fn: ?*const fn (*anyopaque) callconv(.c) void =
            if (header.e_entry != 0) @ptrFromInt(header.e_entry) else null;

        if (found_export) {
            const owner = registry.get(owner_type, owner_name).?;
            owner.entry_fn = entry_fn;
            owner.tests = tests;
        } else {
            registry.put(.{
                .driver_type = owner_type,
                .driver_name = owner_name,
                .vtable_ptr = @ptrFromInt(shared.VIRTUAL_MODULE_LOAD_START),
                .vtable_size = 0,
                .entry_fn = entry_fn,
                .tests = tests,
            }) catch |err| {
                Serial.failLog("Put module into registry");
                return err;
            };
        }
        Serial.okLog("Registered module exports");

        // 4. Queue every `.kmodule.import.<type>[.<name>]` placeholder for
        //    later resolution (once the MMU is enabled).
        for (shdrs) |shdr| {
            const name = sectionName(buf, shstrtab, shdr);
            const rest = stripPrefix(name, shared.IMPORT_SECTION_PREFIX) orelse continue;

            var type_name: []const u8 = rest;
            var inst_name: ?[]const u8 = null;
            if (std.mem.indexOfScalar(u8, rest, '.')) |dot| {
                type_name = rest[0..dot];
                inst_name = rest[dot + 1 ..];
            }

            imports.add(.{
                .type_name = type_name,
                .inst_name = inst_name,
                .parent_type = owner_type,
                .parent_name = owner_name,
                .vtable_ptr = @ptrFromInt(shdr.sh_addr),
            }) catch |err| {
                Serial.failLog("Queued module import");
                return err;
            };
        }
        Serial.okLog("Queued module imports");
    }

    fn parseConfig(
        self: *ModuleLoader,
        root: *uefi.protocol.File,
        page_table: *PageTable,
        registry: *Registry.DriverRegistry,
        imports: *ImportMod.ModuleImportHandles,
        config_buffer: []u8,
    ) !void {
        var line_iter = std.mem.splitScalar(u8, config_buffer, '\n');
        var in_section = false;

        while (line_iter.next()) |raw_line| {
            const line = std.mem.trim(u8, raw_line, " \t\r");

            if (line.len == 0 or line[0] == ';' or line[0] == '#') continue;

            if (line[0] == '[') {
                in_section = std.mem.eql(u8, line, "[Modules]");
                continue;
            }

            if (!in_section) continue;

            var kv_iter = std.mem.splitScalar(u8, line, '=');
            const raw_name = kv_iter.next() orelse continue;
            const raw_status = kv_iter.next() orelse continue;

            const name = std.mem.trim(u8, raw_name, " \t");
            const status = std.mem.trim(u8, raw_status, " \t");

            if (status.len > 0 and (status[0] == 'Y' or status[0] == 'y')) {
                try self.loadModule(root, name, page_table, registry, imports);
            }
        }
    }

    pub fn loadKernel(
        self: *ModuleLoader,
        root: *uefi.protocol.File,
        config_file: [*:0]const u16,
        page_table: *PageTable,
        registry: *Registry.DriverRegistry,
        imports: *ImportMod.ModuleImportHandles,
    ) !void {
        const config_buffer = Filesystem.readFile(config_file, root) catch |err| {
            Serial.failLog("Read kernel configuration");
            return err;
        };
        Serial.okLog("Read kernel configuration");

        try self.parseConfig(root, page_table, registry, imports, config_buffer);
    }
};

// --- Unit tests -------------------------------------------------------
//
// These exercise the pure byte-buffer logic (no UEFI calls), including
// regression coverage for two real bugs found while first bringing the
// bootloader up in QEMU: PT_LOAD segments landing at a non-page-aligned
// p_vaddr (computeSegmentMapping), and modules needing PIE-style
// R_AARCH64_RELATIVE relocations rather than baked-in absolute addresses
// (linkElfModule).

const testing = std.testing;

test "computeSegmentMapping accounts for a non-page-aligned p_vaddr" {
    // Observed in practice: a PIE module's .text PT_LOAD segment at
    // p_vaddr 0x229d0, memsz 0x33c24. Naively mapping starting at p_vaddr
    // put the segment's bytes at the wrong offset within the mapped page,
    // corrupting everything (including the module's own entry point).
    const phdr: Elf64_Phdr = .{
        .p_type = std.elf.PT_LOAD,
        .p_flags = 0,
        .p_offset = 0x129d0,
        .p_vaddr = 0x229d0,
        .p_paddr = 0x229d0,
        .p_filesz = 0x33c24,
        .p_memsz = 0x33c24,
        .p_align = 0x10000,
    };
    const m = computeSegmentMapping(phdr);
    try testing.expectEqual(@as(u64, 0x22000), m.page_aligned_vaddr);
    try testing.expectEqual(@as(u64, 0x9d0), m.offset_in_page);
    try testing.expectEqual(@as(usize, 0x35), m.page_count); // ceil((0x9d0+0x33c24)/0x1000)
}

test "computeSegmentMapping is a no-op adjustment for an already page-aligned segment" {
    const phdr: Elf64_Phdr = .{
        .p_type = std.elf.PT_LOAD,
        .p_flags = 0,
        .p_offset = 0x1000,
        .p_vaddr = 0x5000,
        .p_paddr = 0x5000,
        .p_filesz = 0x1800,
        .p_memsz = 0x2000,
        .p_align = 0x1000,
    };
    const m = computeSegmentMapping(phdr);
    try testing.expectEqual(@as(u64, 0x5000), m.page_aligned_vaddr);
    try testing.expectEqual(@as(u64, 0), m.offset_in_page);
    try testing.expectEqual(@as(usize, 2), m.page_count);
}

/// Hand-assembles a minimal PIE-shaped ELF image in `buf`: one PT_LOAD
/// segment (p_vaddr 0x2000, page-aligned), one `.rela.dyn` section with a
/// single R_AARCH64_RELATIVE entry targeting an address inside that
/// segment, and a `.shstrtab` naming both. Returns the entry-point value
/// used, purely for readability at call sites.
fn buildTestElf(buf: []align(8) u8) void {
    @memset(buf, 0);

    const ehdr_off = 0;
    const phdr_off = 0x40; // right after a 64-byte Ehdr
    const seg_file_off = 0x100;
    const shdr_off = 0x140;
    const rela_off = 0x220;
    const strtab_off = 0x300;

    const rela_dyn_name_off = 1; // buf[strtab_off + 1..] == ".rela.dyn"
    const shstrtab_name_off = rela_dyn_name_off + ".rela.dyn".len + 1;

    const strtab = buf[strtab_off..];
    @memcpy(strtab[rela_dyn_name_off..][0..".rela.dyn".len], ".rela.dyn");
    @memcpy(strtab[shstrtab_name_off..][0..".shstrtab".len], ".shstrtab");

    const ehdr: *Elf64_Ehdr = @ptrCast(@alignCast(buf.ptr + ehdr_off));
    ehdr.* = std.mem.zeroes(Elf64_Ehdr);
    @memcpy(ehdr.e_ident[0..4], std.elf.MAGIC);
    ehdr.e_entry = 0x2004;
    ehdr.e_phoff = phdr_off;
    ehdr.e_shoff = shdr_off;
    ehdr.e_phnum = 1;
    ehdr.e_shnum = 3;
    ehdr.e_shstrndx = 2;

    const phdr: *Elf64_Phdr = @ptrCast(@alignCast(buf.ptr + phdr_off));
    phdr.* = .{
        .p_type = std.elf.PT_LOAD,
        .p_flags = 0,
        .p_offset = seg_file_off,
        .p_vaddr = 0x2000,
        .p_paddr = 0x2000,
        .p_filesz = 0x40,
        .p_memsz = 0x40,
        .p_align = 0x1000,
    };

    const shdrs: [*]Elf64_Shdr = @ptrCast(@alignCast(buf.ptr + shdr_off));
    shdrs[0] = std.mem.zeroes(Elf64_Shdr);
    shdrs[1] = .{
        .sh_name = rela_dyn_name_off,
        .sh_type = std.elf.SHT_RELA,
        .sh_flags = 0,
        .sh_addr = 0x2010,
        .sh_offset = rela_off,
        .sh_size = @sizeOf(Elf64_Rela),
        .sh_link = 0,
        .sh_info = 0,
        .sh_addralign = 8,
        .sh_entsize = @sizeOf(Elf64_Rela),
    };
    shdrs[2] = .{
        .sh_name = shstrtab_name_off,
        .sh_type = std.elf.SHT_STRTAB,
        .sh_flags = 0,
        .sh_addr = 0,
        .sh_offset = strtab_off,
        .sh_size = 64,
        .sh_link = 0,
        .sh_info = 0,
        .sh_addralign = 1,
        .sh_entsize = 0,
    };

    // One R_AARCH64_RELATIVE relocation, targeting VA 0x2010 (file offset
    // 0x100 + (0x2010 - 0x2000) = 0x110), with a distinctive placeholder
    // value so a failure to patch it is obvious.
    const rela: *Elf64_Rela = @ptrCast(@alignCast(buf.ptr + rela_off));
    rela.* = .{
        .r_offset = 0x2010,
        .r_info = @intFromEnum(std.elf.R_AARCH64.RELATIVE),
        .r_addend = 0x5000,
    };
    const target: *align(1) u64 = @ptrCast(buf.ptr + seg_file_off + 0x10);
    target.* = 0xCCCCCCCCCCCCCCCC;
}

test "elfSpan computes the page-aligned span of the PT_LOAD segments" {
    var buf: [1024]u8 align(8) = undefined;
    buildTestElf(&buf);

    const span = try elfSpan(&buf);
    try testing.expectEqual(@as(u64, 0x2000), span.start);
    try testing.expectEqual(@as(u64, 1), span.pages);
}

test "linkElfModule shifts entry/phdr/shdr addresses and applies RELATIVE relocations" {
    var buf: [1024]u8 align(8) = undefined;
    buildTestElf(&buf);

    const delta: u64 = 0x1000_0000_0000;
    try linkElfModule(&buf, delta);

    const header = try elfHeader(&buf);
    try testing.expectEqual(@as(u64, 0x2004) + delta, header.e_entry);

    const phdrs = phdrSlice(&buf, header);
    try testing.expectEqual(@as(u64, 0x2000) + delta, phdrs[0].p_vaddr);
    try testing.expectEqual(@as(u64, 0x2000) + delta, phdrs[0].p_paddr);

    const shdrs = shdrSlice(&buf, header);
    try testing.expectEqual(@as(u64, 0x2010) + delta, shdrs[1].sh_addr);
    try testing.expectEqual(delta, shdrs[2].sh_addr);

    // The relocation target: delta + r_addend (0x5000), not the original
    // 0xCCCCCCCCCCCCCCCC placeholder.
    const target: *align(1) const u64 = @ptrCast(buf[0x110..].ptr);
    try testing.expectEqual(delta +% 0x5000, target.*);
}

test "sectionName resolves a section's name via the shstrtab" {
    var buf: [1024]u8 align(8) = undefined;
    buildTestElf(&buf);

    const header = try elfHeader(&buf);
    const shdrs = shdrSlice(&buf, header);
    const shstrtab = shdrs[header.e_shstrndx];

    try testing.expectEqualStrings(".rela.dyn", sectionName(&buf, shstrtab, shdrs[1]));
    try testing.expectEqualStrings(".shstrtab", sectionName(&buf, shstrtab, shdrs[2]));
}

test "stripPrefix" {
    try testing.expectEqualStrings("dummy.test_driver", stripPrefix(".kmodule.export.dummy.test_driver", shared.EXPORT_SECTION_PREFIX).?);
    try testing.expect(stripPrefix(".text", shared.EXPORT_SECTION_PREFIX) == null);
}

test "segmentContaining finds the PT_LOAD segment covering an address" {
    var buf: [1024]u8 align(8) = undefined;
    buildTestElf(&buf);

    const header = try elfHeader(&buf);
    const phdrs = phdrSlice(&buf, header);

    const found = segmentContaining(phdrs, 0x2010) orelse return error.TestUnexpectedResult;
    try testing.expectEqual(@as(u64, 0x2000), found.p_vaddr);

    try testing.expect(segmentContaining(phdrs, 0x9999) == null);
}
