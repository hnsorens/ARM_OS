const std = @import("std");
const uefi = std.os.uefi;

const serial = @import("logging/serial.zig");
const filesystem = @import("uefi/filesystem.zig");

pub fn main() void {

    serial.boot_log_start();

    _ = filesystem.open_root() catch {
        _ = serial.fail_log("Open Root\n", 10);
        return;
    };

    

    const system_table = uefi.system_table;

    _ = system_table.con_out.?.clearScreen() catch {};

    // Use Zig's standard library compile-time helper for UTF-16 LE null-terminated literals
    const msg = std.unicode.utf8ToUtf16LeStringLiteral("Hello, World!\r\n");
    
    _ = system_table.con_out.?.outputString(msg) catch {};

    while (true) {
        //_ = serial.boot_log("Logging!!\n", 10);
    }

    // begin the code
    
}

