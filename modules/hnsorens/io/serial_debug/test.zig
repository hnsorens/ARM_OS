//! Tests for the PL011 driver in `main.zig`. Split into its own file so
//! `main.zig` stays pure driver logic; reachable from the build only
//! because `main.zig` does `comptime { _ = @import("test.zig"); }`, which
//! forces this file's own top-level `comptime` blocks (the `kernelTest`
//! registrations below) to run too.
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const main = @import("main.zig");

// A plain literal, not an import: importing our own exported "serial"
// category here would make this module depend on itself in the registry's
// dependency graph (an instant cycle -- initOne would recurse into itself
// forever until it hits MAX_DEPTH).
const self_serial: abi.Serial = .{ .write = main.uartWrite };

fn testWriteReturnsLength() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = &self_serial };
    const msg = "serial_debug self-test\n";
    t.expectEqual(@src(), main.uartWrite(msg.ptr, msg.len), msg.len);
    return t.result();
}

fn testWriteEmptyReturnsZero() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = &self_serial };
    const empty = "";
    t.expectEqual(@src(), main.uartWrite(empty.ptr, empty.len), 0);
    return t.result();
}

fn testWriteSingleByte() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = &self_serial };
    const one = "x";
    t.expectEqual(@src(), main.uartWrite(one.ptr, one.len), 1);
    return t.result();
}

fn testWriteNewlineDoesNotShortenLength() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = &self_serial };
    // '\n' is translated to "\r\n" internally (two physical bytes sent),
    // but the reported length must still match the *input* byte count,
    // not the physical wire byte count.
    const msg = "a\nb\nc\n";
    t.expectEqual(@src(), main.uartWrite(msg.ptr, msg.len), msg.len);
    return t.result();
}

fn testWriteAllNewlines() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = &self_serial };
    const msg = "\n\n\n\n\n";
    t.expectEqual(@src(), main.uartWrite(msg.ptr, msg.len), msg.len);
    return t.result();
}

fn testWriteLargeBuffer() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = &self_serial };
    var buf: [2048]u8 = undefined;
    for (&buf, 0..) |*b, i| b.* = @truncate('A' + (i % 26));
    t.expectEqual(@src(), main.uartWrite(&buf, buf.len), buf.len);
    return t.result();
}

fn testWriteRepeatedCallsEachReturnOwnLength() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = &self_serial };
    const a = "first\n";
    const b = "second-message\n";
    const c = "3\n";
    t.expectEqual(@src(), main.uartWrite(a.ptr, a.len), a.len);
    t.expectEqual(@src(), main.uartWrite(b.ptr, b.len), b.len);
    t.expectEqual(@src(), main.uartWrite(c.ptr, c.len), c.len);
    return t.result();
}

comptime {
    abi.kernelTest("write_returns_length", &testWriteReturnsLength);
    abi.kernelTest("write_empty_returns_zero", &testWriteEmptyReturnsZero);
    abi.kernelTest("write_single_byte", &testWriteSingleByte);
    abi.kernelTest("write_newline_does_not_shorten_length", &testWriteNewlineDoesNotShortenLength);
    abi.kernelTest("write_all_newlines", &testWriteAllNewlines);
    abi.kernelTest("write_large_buffer", &testWriteLargeBuffer);
    abi.kernelTest("write_repeated_calls_each_return_own_length", &testWriteRepeatedCallsEachReturnOwnLength);
}
