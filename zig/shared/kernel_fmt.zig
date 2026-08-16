//! Formats a message with `std.fmt` and writes it through a `Serial`
//! interface. See the note on `abi_types.Serial` for why this exists
//! instead of a variadic `printf`.

const std = @import("std");
const abi = @import("abi");

/// Large enough for any realistic single log/test-diagnostic line; silently
/// drops the message on overflow rather than risk relying on partial-write
/// behavior from a failed `bufPrint`.
const BUF_LEN = 1024;

pub fn print(serial: *const abi.Serial, comptime fmt: []const u8, args: anytype) void {
    var buf: [BUF_LEN]u8 = undefined;
    const msg = std.fmt.bufPrint(&buf, fmt, args) catch return;
    _ = serial.write(msg.ptr, msg.len);
}
