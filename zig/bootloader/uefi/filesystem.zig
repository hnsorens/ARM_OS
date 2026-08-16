const std = @import("std");
const uefi = std.os.uefi;

const serial = @import("../logging/serial.zig");

/// Locates the first handle exposing SimpleFileSystem (the boot disk under
/// QEMU/OVMF) and opens its root directory.
pub fn openRoot() !*uefi.protocol.File {
    const bs = uefi.system_table.boot_services.?;

    const handles = (bs.locateHandleBuffer(.{ .by_protocol = &uefi.protocol.SimpleFileSystem.guid }) catch |err| {
        serial.failLog("Found Fat32 volume");
        return err;
    }) orelse {
        serial.failLog("Found Fat32 volume");
        return error.HandlesNotFound;
    };
    defer bs.freePool(@ptrCast(handles.ptr)) catch {};

    const fs = (bs.handleProtocol(uefi.protocol.SimpleFileSystem, handles[0]) catch |err| {
        serial.failLog("Found Filesystem");
        return err;
    }) orelse {
        serial.failLog("Found Filesystem");
        return error.FilesystemNotFound;
    };

    return fs.openVolume() catch |err| {
        serial.failLog("Opening Volume");
        return err;
    };
}

/// Reads an entire file into a freshly pool-allocated, NUL-terminated buffer.
/// The buffer is never freed by the bootloader (matches the C original:
/// boot-services pool memory is simply leaked for the lifetime of the boot).
pub fn readFile(file_name: [*:0]const u16, root: *uefi.protocol.File) ![]align(8) u8 {
    const bs = uefi.system_table.boot_services.?;

    const file = root.open(file_name, .read, .{}) catch |err| {
        serial.failLog("Opened file");
        return err;
    };
    defer file.close() catch {};

    const info_size = file.getInfoSize(.file) catch |err| {
        serial.failLog("Found file info");
        return err;
    };

    const info_buf = bs.allocatePool(.loader_data, info_size) catch |err| {
        serial.failLog("Allocated file info");
        return err;
    };
    defer bs.freePool(info_buf.ptr) catch {};

    const info = file.getInfo(.file, info_buf) catch |err| {
        serial.failLog("Got file info");
        return err;
    };
    const file_size: usize = info.file_size;

    const file_buf = bs.allocatePool(.loader_data, file_size + 1) catch |err| {
        serial.failLog("Allocated file buffer");
        return err;
    };
    errdefer bs.freePool(file_buf.ptr) catch {};

    const read_len = file.read(file_buf[0..file_size]) catch |err| {
        serial.failLog("Read file");
        return err;
    };
    if (read_len != file_size) {
        serial.failLog("Read file (short read)");
        return error.ShortRead;
    }

    file_buf[file_size] = 0;

    return file_buf;
}
