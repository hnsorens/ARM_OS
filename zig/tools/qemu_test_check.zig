//! Reads captured serial output from a QEMU boot of the bootloader image
//! and decides pass/fail for `zig build qemu-test`.
//!
//! The bootloader always halts in a WFI loop after printing its results
//! (there's no real kernel to hand off to yet), so the shell command that
//! produces the log wraps QEMU in `timeout` and always "succeeds" from
//! build.zig's point of view -- this tool is what actually determines the
//! step's pass/fail by inspecting what got printed before the timeout hit.
//!
//! Looks for a line of the form:
//!   [SUMMARY] passed=<N> failed=<N> skipped=<N>
//! and for any "TEST FAIL" or "FAIL]" markers. Exits 0 only if a summary
//! was found, failed == 0, and no such marker appears anywhere in the log
//! (covers both module test failures and bootloader boot-step failures).

const std = @import("std");

pub fn main(init: std.process.Init.Minimal) u8 {
    const argv = init.args.vector;
    if (argv.len < 2) {
        std.debug.print("usage: qemu_test_check <log-file>\n", .{});
        return 1;
    }
    const path = std.mem.span(argv[1]);

    const fd = std.posix.openat(std.posix.AT.FDCWD, path, .{}, 0) catch |err| {
        std.debug.print("could not open {s}: {t}\n", .{ path, err });
        return 1;
    };

    var buf: [1 << 20]u8 = undefined;
    var total: usize = 0;
    while (total < buf.len) {
        const n = std.posix.read(fd, buf[total..]) catch |err| {
            std.debug.print("read error on {s}: {t}\n", .{ path, err });
            return 1;
        };
        if (n == 0) break;
        total += n;
    }
    const data = buf[0..total];

    const marker = "[SUMMARY] passed=";
    const idx = std.mem.indexOf(u8, data, marker) orelse {
        std.debug.print("FAIL: no [SUMMARY] line found in {s} (boot likely hung or crashed before module init finished)\n", .{path});
        printTail(data);
        return 1;
    };

    const rest = data[idx + marker.len ..];
    const line_end = std.mem.indexOfScalar(u8, rest, '\n') orelse rest.len;
    const summary_line = rest[0..line_end];
    std.debug.print("{s}{s}\n", .{ marker, summary_line });

    const failed_marker = "failed=";
    const failed_idx = std.mem.indexOf(u8, summary_line, failed_marker) orelse summary_line.len;
    const failed = parseNum(summary_line[@min(failed_idx + failed_marker.len, summary_line.len)..]);

    var any_fail_line = false;
    var line_it = std.mem.splitScalar(u8, data, '\n');
    while (line_it.next()) |line| {
        if (std.mem.indexOf(u8, line, "TEST FAIL") != null or std.mem.indexOf(u8, line, "FAIL]") != null) {
            std.debug.print("  {s}\n", .{line});
            any_fail_line = true;
        }
    }

    if (failed > 0 or any_fail_line) {
        std.debug.print("RESULT: FAIL\n", .{});
        return 1;
    }
    std.debug.print("RESULT: PASS\n", .{});
    return 0;
}

fn printTail(data: []const u8) void {
    const tail_len = @min(data.len, 2048);
    std.debug.print("--- last {d} bytes of captured output ---\n{s}\n", .{ tail_len, data[data.len - tail_len ..] });
}

fn parseNum(s: []const u8) u32 {
    var v: u32 = 0;
    for (s) |c| {
        if (c < '0' or c > '9') break;
        v = v * 10 + (c - '0');
    }
    return v;
}
