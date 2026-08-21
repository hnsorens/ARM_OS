//! GUID Partition Table reader, exporting `Gpt` (category "gpt"). Ported
//! from `things_to_add/gpt.c`, reading through an imported `BlkDevice`
//! instead of calling driver functions directly.
//!
//! On-disk layouts follow the UEFI spec (GPT header at LBA 1, a
//! caller-defined but conventionally 128-byte partition entry). There was
//! no `gpt.h` in the C reference's `things_to_add/` (it `#include`d one
//! that wasn't provided), so these structs are written directly from the
//! spec rather than ported from a header.
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const phys_mem = @import("phys_mem");

pub const blkdevice_if = abi.importInterface(abi.BlkDevice);
pub const pmm_if = abi.importInterface(abi.Pmm);
pub const serial_if = abi.importInterface(abi.Serial);

// Not used by `readPartitions` itself -- imported so the module's own
// tests can independently discover and stand up a real block device to
// read partitions from (mirroring the `hnsorens.memory.heap` module
// importing `Mmu` purely for `test.zig`'s `get_kernel_ctx` convenience).
pub const virtiobus_if = abi.importInterface(abi.VirtioBus);

pub const SECTOR_SIZE: u64 = 512;
const GPT_HEADER_LBA: u64 = 1;
const GPT_SIGNATURE: u64 = 0x5452415020494645; // "EFI PART", little-endian

const GptHeader = extern struct {
    signature: u64,
    revision: u32,
    header_size: u32,
    header_crc32: u32,
    reserved: u32,
    my_lba: u64,
    alternate_lba: u64,
    first_usable_lba: u64,
    last_usable_lba: u64,
    disk_guid: [16]u8,
    partition_entries_lba: u64,
    num_partition_entries: u32,
    size_of_partition_entry: u32,
    partition_entries_crc32: u32,
};

const GptPartitionEntry = extern struct {
    type_guid: [16]u8,
    unique_guid: [16]u8,
    first_lba: u64,
    last_lba: u64,
    attributes: u64,
    name: [36]u16, // UTF-16LE
};

fn isZeroGuid(guid: [16]u8) bool {
    for (guid) |b| {
        if (b != 0) return false;
    }
    return true;
}

pub fn readPartitions(dev: ?*anyopaque, out_partitions: [*]abi.GptPartition, max_partitions: u32, count_out: *u32) callconv(.c) c_int {
    if (dev == null or max_partitions == 0) return abi.EINVAL;
    count_out.* = 0;

    var header_virt: u64 = undefined;
    var header_phys: u64 = undefined;
    if (phys_mem.alloc(pmm_if, SECTOR_SIZE, &header_virt, &header_phys) != 0) return abi.ENOMEM;
    defer phys_mem.free(pmm_if, header_phys);

    const header_buf: [*]u8 = @ptrFromInt(header_virt);
    if (blkdevice_if.read_sectors(dev, GPT_HEADER_LBA, header_buf, 1) != 0) return abi.EIO;

    const header: *const GptHeader = @ptrCast(@alignCast(header_buf));
    if (header.signature != GPT_SIGNATURE) return abi.EIO;
    if (header.num_partition_entries == 0 or header.size_of_partition_entry == 0) return abi.EIO;

    const entries_bytes: u64 = @as(u64, header.num_partition_entries) * header.size_of_partition_entry;
    const entries_sectors = (entries_bytes + SECTOR_SIZE - 1) / SECTOR_SIZE;

    var entries_virt: u64 = undefined;
    var entries_phys: u64 = undefined;
    if (phys_mem.alloc(pmm_if, entries_sectors * SECTOR_SIZE, &entries_virt, &entries_phys) != 0) return abi.ENOMEM;
    defer phys_mem.free(pmm_if, entries_phys);

    const entries_buf: [*]u8 = @ptrFromInt(entries_virt);
    if (blkdevice_if.read_sectors(dev, header.partition_entries_lba, entries_buf, entries_sectors) != 0) return abi.EIO;

    var found: u32 = 0;
    var i: u32 = 0;
    while (i < header.num_partition_entries and found < max_partitions) : (i += 1) {
        const entry_offset = @as(u64, i) * header.size_of_partition_entry;
        if (entry_offset + @sizeOf(GptPartitionEntry) > entries_bytes) break;

        const entry: *const GptPartitionEntry = @ptrCast(@alignCast(entries_buf + entry_offset));
        if (isZeroGuid(entry.type_guid)) continue;

        const out = &out_partitions[found];
        out.* = .{};
        out.type_guid = entry.type_guid;
        out.unique_guid = entry.unique_guid;
        out.first_lba = entry.first_lba;
        out.last_lba = entry.last_lba;
        out.attributes = entry.attributes;

        // Narrow UTF-16LE to ASCII/Latin-1 (adequate for this OS's plain-
        // ASCII partition labels) and NUL-terminate.
        var j: usize = 0;
        while (j < entry.name.len and j < out.name.len - 1 and entry.name[j] != 0) : (j += 1) {
            out.name[j] = @truncate(entry.name[j]);
        }
        out.name[j] = 0;

        found += 1;
    }

    count_out.* = found;
    return 0;
}

comptime {
    abi.exportInterface("gpt", abi.Gpt, .{
        .read_partitions = readPartitions,
    });
}

comptime {
    _ = @import("test.zig");
}
