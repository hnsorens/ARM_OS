//! Assertion helpers for `abi.kernelTest` test functions, mirroring the C
//! module system's `EXPECT_EQ`/`EXPECT_TRUE`/... macros: each check prints
//! a diagnostic through the caller's `Serial` import on failure and
//! tracks whether any check failed, so a test function can run several
//! checks and report failure if any of them failed (rather than stopping
//! at the first one).
//!
//! Usage:
//!   fn myTest() callconv(.c) i32 {
//!       var t = kernel_test.Tracker{ .serial = serial_if };
//!       t.expectEqual(@src(), foo(), 42);
//!       t.expectTrue(@src(), bar());
//!       return t.result();
//!   }

const std = @import("std");
const abi = @import("abi");
const kernel_fmt = @import("kernel_fmt");

pub const Tracker = struct {
    serial: *const abi.Serial,
    failed: bool = false,

    pub fn result(self: Tracker) i32 {
        return if (self.failed) abi.TEST_FAIL else abi.TEST_PASS;
    }

    pub fn expectEqual(self: *Tracker, src: std.builtin.SourceLocation, actual: anytype, expected: @TypeOf(actual)) void {
        if (actual == expected) return;
        self.failed = true;
        kernel_fmt.print(self.serial, "[  ERROR  ] {s}:{d}: expected {any} == {any}\n", .{ src.file, src.line, actual, expected });
    }

    pub fn expectNotEqual(self: *Tracker, src: std.builtin.SourceLocation, actual: anytype, expected: @TypeOf(actual)) void {
        if (actual != expected) return;
        self.failed = true;
        kernel_fmt.print(self.serial, "[  ERROR  ] {s}:{d}: expected {any} != {any}\n", .{ src.file, src.line, actual, expected });
    }

    pub fn expectLessThan(self: *Tracker, src: std.builtin.SourceLocation, actual: anytype, expected: @TypeOf(actual)) void {
        if (actual < expected) return;
        self.failed = true;
        kernel_fmt.print(self.serial, "[  ERROR  ] {s}:{d}: expected {any} < {any}\n", .{ src.file, src.line, actual, expected });
    }

    pub fn expectLessOrEqual(self: *Tracker, src: std.builtin.SourceLocation, actual: anytype, expected: @TypeOf(actual)) void {
        if (actual <= expected) return;
        self.failed = true;
        kernel_fmt.print(self.serial, "[  ERROR  ] {s}:{d}: expected {any} <= {any}\n", .{ src.file, src.line, actual, expected });
    }

    pub fn expectTrue(self: *Tracker, src: std.builtin.SourceLocation, cond: bool) void {
        if (cond) return;
        self.failed = true;
        kernel_fmt.print(self.serial, "[  ERROR  ] {s}:{d}: expected condition to be TRUE\n", .{ src.file, src.line });
    }

    pub fn expectFalse(self: *Tracker, src: std.builtin.SourceLocation, cond: bool) void {
        if (!cond) return;
        self.failed = true;
        kernel_fmt.print(self.serial, "[  ERROR  ] {s}:{d}: expected condition to be FALSE\n", .{ src.file, src.line });
    }
};
