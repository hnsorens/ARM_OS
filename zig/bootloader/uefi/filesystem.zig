const std = @import("std");
const uefi = std.os.uefi;

const serial = @import("../logging/serial.zig");

// Note: Using uefi.protocol.File instead of protocols.FileProtocol
pub fn open_root() !*uefi.protocol.File {
    const bs = uefi.system_table.boot_services.?;

    const handles_maybe = bs.locateHandleBuffer(
        .{ .by_protocol = &uefi.protocol.SimpleFileSystem.guid }
    ) catch |err| {
        _ = serial.fail_log("Found Fat32 volume\n", 19);
        return err;
    };

    const handles = handles_maybe orelse {
        _ = serial.fail_log("Found Fat32 volume\n", 19);
        return error.HandlesNotFound;
    };

    defer _ = bs.freePool(@ptrCast(handles.ptr)) catch {};

    const fs_maybe = bs.handleProtocol(
        uefi.protocol.SimpleFileSystem,
        handles[0]
    ) catch |err| {
        _ = serial.fail_log("Found Filesystem\n", 17);
        return err;
    };

    const fs = fs_maybe orelse {
        _ = serial.fail_log("Found Filesystem\n", 17);
        return error.FilesystemNotFound;
    };

    return fs.openVolume() catch |err| {
        _ = serial.fail_log("Opening Volume\n", 15);
        return err;
    };
}

pub fn read_file(file_name: [*:0]const u16, root: *uefi.protocol.File) ![]align(8) u8 {
    const bs = uefi.system_table.boot_services.?;

    const File = uefi.protocol.File;

    const file_maybe = root.open(
        file_name,
        File.OpenMode.read,
        .{}
    ) catch |err| {
        _ = serial.fail_log("Opened File\n", 12);
        return err;
    };

    const file: File = file_maybe orelse {
        _ = serial.fail_log("Opened File\n", 12);
        return error.FileNotFound;
    };

    const size_bytes  = @sizeOf(uefi.protocol.File.Info) + 256;

    const raw_info_buffer = bs.allocatePool(.loader_data, size_bytes) catch {};
    const info_buffer: *File.Info.File = @ptrCast(@alignCast(raw_info_buffer.ptr));
    defer _ = bs.freePool(info_buffer) catch {};

    const file_info: File.Info.File = file.getInfo(.file, info_buffer) catch |err| {
        _ = serial.fail_log("Get File Info\n", 14);
        return err;
    };

    const file_size: usize = file_info.file_size;

    const raw_file_buffer = bs.allocatePool(.loader_data, file_size + 1) catch {};

    _ = file.read(raw_info_buffer) catch |err| {
        _ = serial.fail_log("Reading File\n", 13);
        _ = bs.freePool(raw_info_buffer) catch {};
        _ = bs.freePool(raw_file_buffer) catch {};
        return err;
    };

    raw_file_buffer[file_size] = 0;

    file.close();

    return raw_file_buffer;
}
