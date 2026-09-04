//! kernelTests for the tty line discipline. Bytes are injected straight
//! into `ttyRxByte` (what the keyboard ISR calls); `read` is non-blocking
//! here because the tests don't run on a scheduled process, except
//! `blocking_read_wakes_on_line`, which spawns a kernel thread so the
//! block/wake path is exercised for real.
const std = @import("std");
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const main = @import("main.zig");

const serial_if = main.serial_if;
const sched_if = main.sched_if;
const process_if = main.process_if;

fn feed(s: []const u8) void {
    for (s) |c| main.ttyRxByte(c);
}

fn readAll(t: *kernel_test.Tracker, want: []const u8) void {
    var buf: [512]u8 = undefined;
    const n = main.read(&buf, buf.len);
    t.expectEqual(@src(), n, want.len);
    var ok = true;
    for (0..n) |i| {
        if (buf[i] != want[i]) ok = false;
    }
    t.expectTrue(@src(), ok);
}

// --- 0. TCSETS c_lflag actually drives the line discipline ----

fn testTermiosDrivesMode() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    main.testReset();

    var tio: [abi.KTERMIOS_SIZE]u8 = undefined;
    main.tcgets(&tio);
    var lflag = std.mem.readInt(u32, tio[12..16], .little);
    t.expectTrue(@src(), (lflag & abi.ICANON) != 0); // default is canonical

    lflag &= ~(abi.ICANON | abi.ECHO); // -> raw, no echo
    std.mem.writeInt(u32, tio[12..16], lflag, .little);
    main.tcsets(&tio);

    feed("xy"); // raw: delivered immediately, no Enter needed
    readAll(&t, "xy");

    main.tcgets(&tio);
    t.expectTrue(@src(), (std.mem.readInt(u32, tio[12..16], .little) & abi.ICANON) == 0);
    return t.result();
}

// --- 1. raw mode passes bytes straight through ------------

fn testRawPassthrough() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    main.testReset();
    main.setMode(false, false);
    feed("abc");
    readAll(&t, "abc");
    return t.result();
}

// --- 2. canonical mode only delivers on Enter -----------

fn testCanonicalDeliversOnNewline() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    main.testReset();
    feed("hello");
    var buf: [16]u8 = undefined;
    t.expectEqual(@src(), main.read(&buf, 16), 0); // not committed yet
    feed("\n");
    readAll(&t, "hello\n");
    return t.result();
}

// --- 3. backspace edits the pending line ---------------

fn testBackspaceEdits() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    main.testReset();
    feed("hi");
    feed(&[_]u8{0x78}); // 'x'
    feed(&[_]u8{0x7F}); // DEL -> removes 'x'
    feed("\n");
    readAll(&t, "hi\n");
    return t.result();
}

// --- 4. ^U kills the whole pending line ----------------

fn testCtrlUKillsLine() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    main.testReset();
    feed("garbage");
    feed(&[_]u8{0x15}); // ^U
    feed("ok\n");
    readAll(&t, "ok\n");
    return t.result();
}

// --- 5. ^W kills the last word ------------------------

fn testCtrlWKillsWord() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    main.testReset();
    feed("foo bar");
    feed(&[_]u8{0x17}); // ^W -> removes "bar"
    feed("\n");
    readAll(&t, "foo \n");
    return t.result();
}

// --- 6. ^D on an empty line is EOF -------------------

fn testCtrlDEmptyIsEof() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    main.testReset();
    feed(&[_]u8{0x04}); // ^D
    t.expectTrue(@src(), main.testEofPending());

    var buf: [8]u8 = undefined;
    t.expectEqual(@src(), main.read(&buf, 8), 0); // EOF read
    t.expectFalse(@src(), main.testEofPending());
    return t.result();
}

// --- 7. ^D mid-line delivers the partial line --------

fn testCtrlDMidlinePartial() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    main.testReset();
    feed("par");
    feed(&[_]u8{0x04}); // ^D -> deliver "par" now, no newline
    readAll(&t, "par");
    return t.result();
}

// --- 8. CR is translated to LF ----------------------

fn testCrToLf() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    main.testReset();
    feed("x");
    feed(&[_]u8{'\r'});
    readAll(&t, "x\n");
    return t.result();
}

// --- 9. raw partial reads --------------------------

fn testRawPartialReads() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    main.testReset();
    main.setMode(false, false);
    feed("tes");

    var buf: [8]u8 = undefined;
    t.expectEqual(@src(), main.read(&buf, 2), 2);
    t.expectTrue(@src(), buf[0] == 't' and buf[1] == 'e');
    t.expectEqual(@src(), main.read(&buf, 8), 1);
    t.expectEqual(@src(), buf[0], @as(u8, 's'));
    return t.result();
}

// --- 10. switching to raw discards a half-typed line -

fn testModeSwitchDiscardsPartial() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    main.testReset();
    feed("abc"); // pending in the canonical line buffer
    main.setMode(false, false); // discards it
    feed("d");
    readAll(&t, "d");
    return t.result();
}

// --- 11. write returns len -------------------------

fn testWriteReturnsLen() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const msg = "[tty test write]\n";
    t.expectEqual(@src(), main.write(msg, msg.len), msg.len);
    return t.result();
}

// --- 12. a blocked reader is woken when a line commits

var g_tresult: u64 = 999;
var g_tbuf: [64]u8 = undefined;

fn readerEntry(arg: usize) callconv(.c) void {
    _ = arg;
    g_tresult = main.read(&g_tbuf, 64);
    sched_if.exit_current();
}

fn testBlockingReadWakesOnLine() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    main.testReset();
    g_tresult = 999;

    var pid: u32 = 0;
    t.expectEqual(@src(), process_if.create_kernel_thread("ttyr", @intFromPtr(&readerEntry), 0, 4, 0, &pid), 0);
    t.expectEqual(@src(), sched_if.admit(pid), 0);

    t.expectEqual(@src(), sched_if.run(), 0); // reader blocks on empty console
    t.expectEqual(@src(), g_tresult, 999);

    var st: abi.ProcessState = .dead;
    t.expectEqual(@src(), process_if.get_state(pid, &st), 0);
    t.expectEqual(@src(), st, abi.ProcessState.blocked);

    feed("test\n"); // the '\n' commits the line and wakes the reader

    t.expectEqual(@src(), sched_if.run(), 0);
    t.expectEqual(@src(), g_tresult, 5);
    t.expectTrue(@src(), g_tbuf[0] == 't' and g_tbuf[3] == 't' and g_tbuf[4] == '\n');

    t.expectEqual(@src(), process_if.get_state(pid, &st), 0);
    t.expectEqual(@src(), st, abi.ProcessState.zombie);
    t.expectEqual(@src(), process_if.destroy(pid), 0);
    return t.result();
}

comptime {
    abi.kernelTest("termios_drives_mode", &testTermiosDrivesMode);
    abi.kernelTest("raw_passthrough", &testRawPassthrough);
    abi.kernelTest("canonical_delivers_on_newline", &testCanonicalDeliversOnNewline);
    abi.kernelTest("backspace_edits", &testBackspaceEdits);
    abi.kernelTest("ctrl_u_kills_line", &testCtrlUKillsLine);
    abi.kernelTest("ctrl_w_kills_word", &testCtrlWKillsWord);
    abi.kernelTest("ctrl_d_empty_is_eof", &testCtrlDEmptyIsEof);
    abi.kernelTest("ctrl_d_midline_partial", &testCtrlDMidlinePartial);
    abi.kernelTest("cr_to_lf", &testCrToLf);
    abi.kernelTest("raw_partial_reads", &testRawPartialReads);
    abi.kernelTest("mode_switch_discards_partial", &testModeSwitchDiscardsPartial);
    abi.kernelTest("write_returns_len", &testWriteReturnsLen);
    abi.kernelTest("blocking_read_wakes_on_line", &testBlockingReadWakesOnLine);
}
