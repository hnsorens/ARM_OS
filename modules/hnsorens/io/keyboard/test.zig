//! kernelTests for the keyboard/console-input module. The ring buffer,
//! CR->LF translation and the empty-read path are checked directly;
//! `feed_wakes_blocked_reader` is a real block/wake integration test --
//! a kernel thread calls `readBlocking`, blocks on the empty ring, and
//! only completes once `feedByte` (as the ISR would) buffers bytes and
//! wakes it through the scheduler.
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const main = @import("main.zig");

const serial_if = main.serial_if;
const sched_if = main.sched_if;
const process_if = main.process_if;

const RING_SIZE = 256;

fn drain() void {
    var tmp: [RING_SIZE]u8 = undefined;
    _ = main.read(&tmp, RING_SIZE);
}

// --- 1. empty ring -------------------------------------------

fn testInitialStateEmpty() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    drain();
    t.expectEqual(@src(), main.available(), 0);
    var buf: [16]u8 = undefined;
    t.expectEqual(@src(), main.read(&buf, 16), 0);
    return t.result();
}

// --- 2. FIFO order -----------------------------------------

fn testRingFifo() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    drain();

    for ("abcde") |c| main.testPush(c);
    t.expectEqual(@src(), main.available(), 5);

    var buf: [8]u8 = undefined;
    t.expectEqual(@src(), main.read(&buf, 3), 3);
    t.expectTrue(@src(), buf[0] == 'a' and buf[1] == 'b' and buf[2] == 'c');
    t.expectEqual(@src(), main.available(), 2);

    t.expectEqual(@src(), main.read(&buf, 8), 2);
    t.expectTrue(@src(), buf[0] == 'd' and buf[1] == 'e');
    t.expectEqual(@src(), main.available(), 0);
    t.expectEqual(@src(), main.read(&buf, 8), 0);
    return t.result();
}

// --- 3. head/tail wrap past RING_SIZE --------------------

fn testRingWraparound() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    drain();

    var i: usize = 0;
    while (i < 250) : (i += 1) main.testPush(@truncate(i));

    var buf: [260]u8 = undefined;
    t.expectEqual(@src(), main.read(&buf, 240), 240);
    var ok = true;
    for (0..240) |k| {
        if (buf[k] != @as(u8, @truncate(k))) ok = false;
    }
    t.expectTrue(@src(), ok);
    t.expectEqual(@src(), main.available(), 10); // 250 - 240

    i = 250;
    while (i < 262) : (i += 1) main.testPush(@truncate(i));
    t.expectEqual(@src(), main.available(), 22);

    t.expectEqual(@src(), main.read(&buf, 260), 22);
    for (0..22) |k| {
        if (buf[k] != @as(u8, @truncate(240 + k))) ok = false;
    }
    t.expectTrue(@src(), ok);
    return t.result();
}

// --- 4. overrun drops the newest, keeps the oldest ------

fn testRingOverrunDrops() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    drain();

    var i: usize = 0;
    while (i < RING_SIZE + 20) : (i += 1) main.testPush(@truncate(i));
    t.expectEqual(@src(), main.available(), RING_SIZE);

    var buf: [RING_SIZE]u8 = undefined;
    t.expectEqual(@src(), main.read(&buf, RING_SIZE), RING_SIZE);
    var ok = true;
    for (0..RING_SIZE) |k| {
        if (buf[k] != @as(u8, @truncate(k))) ok = false;
    }
    t.expectTrue(@src(), ok);
    return t.result();
}

// --- 5. CR is translated to LF --------------------------

fn testFeedTranslatesCrToLf() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    drain();

    main.feedByte('x');
    main.feedByte('\r');

    var buf: [4]u8 = undefined;
    t.expectEqual(@src(), main.read(&buf, 4), 2);
    t.expectEqual(@src(), buf[0], @as(u8, 'x'));
    t.expectEqual(@src(), buf[1], @as(u8, '\n'));
    return t.result();
}

// --- 6. a blocked reader is woken by feedByte ----------

var g_kb_result: u64 = 999;
var g_kb_buf: [8]u8 = undefined;

fn readerEntry(arg: usize) callconv(.c) void {
    _ = arg;
    g_kb_result = main.readBlocking(&g_kb_buf, 4);
    sched_if.exit_current();
}

fn testFeedWakesBlockedReader() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    drain();
    g_kb_result = 999;

    var pid: u32 = 0;
    t.expectEqual(@src(), process_if.create_kernel_thread("kbr", @intFromPtr(&readerEntry), 0, 4, 0, &pid), 0);
    t.expectEqual(@src(), sched_if.admit(pid), 0);

    // Reader runs, finds the ring empty, blocks -> run() returns.
    t.expectEqual(@src(), sched_if.run(), 0);
    t.expectEqual(@src(), g_kb_result, 999);

    var st: abi.ProcessState = .dead;
    t.expectEqual(@src(), process_if.get_state(pid, &st), 0);
    t.expectEqual(@src(), st, abi.ProcessState.blocked);

    // Feed 4 bytes as the ISR would; the first wakes the reader.
    main.feedByte('t');
    main.feedByte('e');
    main.feedByte('s');
    main.feedByte('t');

    // Reader wakes, reads 4, exits -> run() returns.
    t.expectEqual(@src(), sched_if.run(), 0);
    t.expectEqual(@src(), g_kb_result, 4);
    t.expectTrue(@src(), g_kb_buf[0] == 't' and g_kb_buf[1] == 'e' and g_kb_buf[2] == 's' and g_kb_buf[3] == 't');

    t.expectEqual(@src(), process_if.get_state(pid, &st), 0);
    t.expectEqual(@src(), st, abi.ProcessState.zombie);
    t.expectEqual(@src(), process_if.destroy(pid), 0);
    return t.result();
}

comptime {
    abi.kernelTest("initial_state_empty", &testInitialStateEmpty);
    abi.kernelTest("ring_fifo", &testRingFifo);
    abi.kernelTest("ring_wraparound", &testRingWraparound);
    abi.kernelTest("ring_overrun_drops", &testRingOverrunDrops);
    abi.kernelTest("feed_translates_cr_to_lf", &testFeedTranslatesCrToLf);
    abi.kernelTest("feed_wakes_blocked_reader", &testFeedWakesBlockedReader);
}
