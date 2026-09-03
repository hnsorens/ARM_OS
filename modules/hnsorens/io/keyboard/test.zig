//! kernelTests for the raw keyboard device: the fallback ring buffer
//! (used when nothing has registered a listener) and listener delivery.
//! Line-discipline behaviour is tested in `hnsorens.io.tty`.
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const main = @import("main.zig");

const serial_if = main.serial_if;

const RING_SIZE = 256;

fn drain() void {
    var tmp: [RING_SIZE]u8 = undefined;
    _ = main.read(&tmp, RING_SIZE);
}

// --- 1. empty ring -------------------------------------------

fn testInitialStateEmpty() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    main.setListener(null);
    drain();
    t.expectEqual(@src(), main.available(), 0);
    var buf: [16]u8 = undefined;
    t.expectEqual(@src(), main.read(&buf, 16), 0);
    return t.result();
}

// --- 2. FIFO order (no listener -> ring) --------------------

fn testRingFifo() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    main.setListener(null);
    drain();

    for ("abcde") |c| main.feedByte(c);
    t.expectEqual(@src(), main.available(), 5);

    var buf: [8]u8 = undefined;
    t.expectEqual(@src(), main.read(&buf, 3), 3);
    t.expectTrue(@src(), buf[0] == 'a' and buf[1] == 'b' and buf[2] == 'c');
    t.expectEqual(@src(), main.available(), 2);

    t.expectEqual(@src(), main.read(&buf, 8), 2);
    t.expectTrue(@src(), buf[0] == 'd' and buf[1] == 'e');
    t.expectEqual(@src(), main.available(), 0);
    return t.result();
}

// --- 3. head/tail wrap past RING_SIZE --------------------

fn testRingWraparound() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    main.setListener(null);
    drain();

    var i: usize = 0;
    while (i < 250) : (i += 1) main.feedByte(@truncate(i));

    var buf: [260]u8 = undefined;
    t.expectEqual(@src(), main.read(&buf, 240), 240);
    var ok = true;
    for (0..240) |k| {
        if (buf[k] != @as(u8, @truncate(k))) ok = false;
    }
    t.expectTrue(@src(), ok);
    t.expectEqual(@src(), main.available(), 10);

    i = 250;
    while (i < 262) : (i += 1) main.feedByte(@truncate(i));
    t.expectEqual(@src(), main.available(), 22);

    t.expectEqual(@src(), main.read(&buf, 260), 22);
    for (0..22) |k| {
        if (buf[k] != @as(u8, @truncate(240 + k))) ok = false;
    }
    t.expectTrue(@src(), ok);
    return t.result();
}

// --- 4. overrun drops the newest --------------------------

fn testRingOverrunDrops() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    main.setListener(null);
    drain();

    var i: usize = 0;
    while (i < RING_SIZE + 20) : (i += 1) main.feedByte(@truncate(i));
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

// --- 5. a registered listener gets bytes; the ring does not

var g_seen: [8]u8 = undefined;
var g_seen_n: usize = 0;

fn recordListener(b: u8) callconv(.c) void {
    if (g_seen_n < g_seen.len) {
        g_seen[g_seen_n] = b;
        g_seen_n += 1;
    }
}

fn testListenerDelivery() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    drain();
    g_seen_n = 0;

    main.setListener(&recordListener);
    for ("hi!") |c| main.feedByte(c);

    t.expectEqual(@src(), g_seen_n, 3);
    t.expectTrue(@src(), g_seen[0] == 'h' and g_seen[1] == 'i' and g_seen[2] == '!');
    // Nothing leaked into the fallback ring while a listener was set.
    t.expectEqual(@src(), main.available(), 0);

    main.setListener(null);
    return t.result();
}

comptime {
    abi.kernelTest("initial_state_empty", &testInitialStateEmpty);
    abi.kernelTest("ring_fifo", &testRingFifo);
    abi.kernelTest("ring_wraparound", &testRingWraparound);
    abi.kernelTest("ring_overrun_drops", &testRingOverrunDrops);
    abi.kernelTest("listener_delivery", &testListenerDelivery);
}
