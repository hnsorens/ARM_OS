//! kernelTests for the cooperative scheduler. Each test spawns real
//! kernel threads (via the process module), admits them, calls `run`, and
//! asserts on an execution trace the tasks build as they are scheduled --
//! so these exercise the process module, the context switcher, and the
//! scheduler together. Every test destroys the (now zombie) tasks it
//! created so `pmm` stays balanced.
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const main = @import("main.zig");

const serial_if = main.serial_if;
const process_if = main.process_if;

fn createThread(t: *kernel_test.Tracker, name: [*:0]const u8, entry: usize, arg: usize) u32 {
    var pid: u32 = 0;
    t.expectEqual(@src(), process_if.create_kernel_thread(name, entry, arg, 4, 0, &pid), 0);
    return pid;
}

fn spawnAdmit(t: *kernel_test.Tracker, name: [*:0]const u8, entry: usize, arg: usize) u32 {
    const pid = createThread(t, name, entry, arg);
    t.expectEqual(@src(), main.admit(pid), 0);
    return pid;
}

// --- 1. run() with an empty queue returns immediately -----------

fn testRunEmptyReturns() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.run(), 0);
    t.expectEqual(@src(), main.queueLen(), 0);
    t.expectEqual(@src(), main.current(), 0);
    return t.result();
}

// --- 2. one task runs then exits -----------------------------

var g_s2: u32 = 0;

fn s2Entry(arg: usize) callconv(.c) void {
    _ = arg;
    g_s2 += 1;
    main.exitCurrent();
}

fn testSingleTaskRunsAndExits() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    g_s2 = 0;
    const pid = spawnAdmit(&t, "s2", @intFromPtr(&s2Entry), 0);
    t.expectEqual(@src(), main.queueLen(), 1);

    t.expectEqual(@src(), main.run(), 0);
    t.expectEqual(@src(), g_s2, 1);
    t.expectEqual(@src(), main.queueLen(), 0);
    t.expectEqual(@src(), main.current(), 0);

    t.expectEqual(@src(), process_if.destroy(pid), 0);
    return t.result();
}

// --- 3. round-robin visits tasks in admission order ----------

var g_rr_trace: [64]u8 = undefined;
var g_rr_len: usize = 0;

fn rrPush(c: u8) void {
    if (g_rr_len < g_rr_trace.len) {
        g_rr_trace[g_rr_len] = c;
        g_rr_len += 1;
    }
}

fn rrEntry(arg: usize) callconv(.c) void {
    const id: u8 = @intCast('1' + arg);
    var rounds: u32 = 0;
    while (rounds < 4) : (rounds += 1) {
        rrPush(id);
        main.yield();
    }
    main.exitCurrent();
}

fn testRoundRobinOrder() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    g_rr_len = 0;
    var pids: [3]u32 = undefined;
    for (0..3) |i| pids[i] = spawnAdmit(&t, "rr", @intFromPtr(&rrEntry), i);

    t.expectEqual(@src(), main.run(), 0);

    t.expectEqual(@src(), g_rr_len, 12);
    var ok = true;
    for (0..g_rr_len) |i| {
        if (g_rr_trace[i] != @as(u8, @intCast('1' + (i % 3)))) ok = false;
    }
    t.expectTrue(@src(), ok);

    for (pids) |p| t.expectEqual(@src(), process_if.destroy(p), 0);
    return t.result();
}

// --- 4. yield with a single ready task is a fast no-op -------

var g_y: u32 = 0;

fn yEntry(arg: usize) callconv(.c) void {
    _ = arg;
    var i: u32 = 0;
    while (i < 50) : (i += 1) {
        g_y += 1;
        main.yield();
    }
    main.exitCurrent();
}

fn testYieldSingleTaskNoop() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    g_y = 0;
    const pid = spawnAdmit(&t, "y", @intFromPtr(&yEntry), 0);
    t.expectEqual(@src(), main.run(), 0);
    t.expectEqual(@src(), g_y, 50);
    t.expectEqual(@src(), process_if.destroy(pid), 0);
    return t.result();
}

// --- 5. block / wake --------------------------------------

var g_bw_trace: [16]u8 = undefined;
var g_bw_len: usize = 0;
var g_bw_a_pid: u32 = 0;

fn bwPush(c: u8) void {
    if (g_bw_len < g_bw_trace.len) {
        g_bw_trace[g_bw_len] = c;
        g_bw_len += 1;
    }
}

fn bwEntryA(arg: usize) callconv(.c) void {
    _ = arg;
    bwPush('a');
    _ = main.block();
    bwPush('A');
    main.exitCurrent();
}

fn bwEntryB(arg: usize) callconv(.c) void {
    _ = arg;
    bwPush('b');
    _ = main.wake(g_bw_a_pid);
    bwPush('B');
    main.exitCurrent();
}

fn testBlockAndWake() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    g_bw_len = 0;
    g_bw_a_pid = spawnAdmit(&t, "A", @intFromPtr(&bwEntryA), 0);
    const b = spawnAdmit(&t, "B", @intFromPtr(&bwEntryB), 0);

    t.expectEqual(@src(), main.run(), 0);

    t.expectEqual(@src(), g_bw_len, 4);
    t.expectEqual(@src(), g_bw_trace[0], @as(u8, 'a'));
    t.expectEqual(@src(), g_bw_trace[1], @as(u8, 'b'));
    t.expectEqual(@src(), g_bw_trace[2], @as(u8, 'B'));
    t.expectEqual(@src(), g_bw_trace[3], @as(u8, 'A'));

    t.expectEqual(@src(), process_if.destroy(g_bw_a_pid), 0);
    t.expectEqual(@src(), process_if.destroy(b), 0);
    return t.result();
}

// --- 6. exit_current drops a task from rotation ------------

var g_er_trace: [32]u8 = undefined;
var g_er_len: usize = 0;

fn erPush(c: u8) void {
    if (g_er_len < g_er_trace.len) {
        g_er_trace[g_er_len] = c;
        g_er_len += 1;
    }
}

fn erEntry(arg: usize) callconv(.c) void {
    const id: u8 = @intCast('1' + arg);
    if (id == '2') {
        erPush('2');
        main.exitCurrent();
    }
    var r: u32 = 0;
    while (r < 3) : (r += 1) {
        erPush(id);
        main.yield();
    }
    main.exitCurrent();
}

fn testExitRemovesFromRotation() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    g_er_len = 0;
    var pids: [3]u32 = undefined;
    for (0..3) |i| pids[i] = spawnAdmit(&t, "er", @intFromPtr(&erEntry), i);

    t.expectEqual(@src(), main.run(), 0);

    t.expectEqual(@src(), g_er_len, 7);
    var twos: u32 = 0;
    for (0..g_er_len) |i| {
        if (g_er_trace[i] == '2') twos += 1;
    }
    t.expectEqual(@src(), twos, 1);
    // "1231313"
    const want = "1231313";
    var ok = true;
    for (0..g_er_len) |i| {
        if (g_er_trace[i] != want[i]) ok = false;
    }
    t.expectTrue(@src(), ok);

    for (pids) |p| t.expectEqual(@src(), process_if.destroy(p), 0);
    return t.result();
}

// --- 7. admit validation --------------------------------

var g_self_admit_rc: c_int = 0;

fn selfAdmitEntry(arg: usize) callconv(.c) void {
    _ = arg;
    g_self_admit_rc = main.admit(main.current());
    main.exitCurrent();
}

fn testAdmitValidation() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.admit(9_999_999), abi.EINVAL);

    const pid = createThread(&t, "adm", @intFromPtr(&s2Entry), 0);
    t.expectEqual(@src(), main.admit(pid), 0);
    t.expectEqual(@src(), main.admit(pid), abi.EEXIST); // already queued
    t.expectEqual(@src(), main.remove(pid), 0);
    t.expectEqual(@src(), process_if.destroy(pid), 0);

    // A task can't re-admit itself while it is the running task.
    g_self_admit_rc = 0;
    const sp = spawnAdmit(&t, "self", @intFromPtr(&selfAdmitEntry), 0);
    t.expectEqual(@src(), main.run(), 0);
    t.expectEqual(@src(), g_self_admit_rc, abi.EEXIST);
    t.expectEqual(@src(), process_if.destroy(sp), 0);
    return t.result();
}

// --- 8. remove dequeues without destroying -------------

var g_rd: u32 = 0;

fn rdEntry(arg: usize) callconv(.c) void {
    _ = arg;
    g_rd += 1;
    main.exitCurrent();
}

fn testRemoveDequeues() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    g_rd = 0;
    const a = spawnAdmit(&t, "a", @intFromPtr(&rdEntry), 0);
    const b = spawnAdmit(&t, "b", @intFromPtr(&rdEntry), 0);
    const c = spawnAdmit(&t, "c", @intFromPtr(&rdEntry), 0);
    t.expectEqual(@src(), main.queueLen(), 3);

    t.expectEqual(@src(), main.remove(b), 0);
    t.expectEqual(@src(), main.queueLen(), 2);
    t.expectEqual(@src(), main.remove(b), abi.EINVAL); // no longer queued

    t.expectEqual(@src(), main.run(), 0);
    t.expectEqual(@src(), g_rd, 2); // only a and c ran

    for ([_]u32{ a, b, c }) |p| t.expectEqual(@src(), process_if.destroy(p), 0);
    return t.result();
}

// --- 9. wake validation --------------------------------

fn testWakeValidation() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const pid = spawnAdmit(&t, "wv", @intFromPtr(&s2Entry), 0);
    t.expectEqual(@src(), main.wake(pid), abi.EINVAL); // ready, not blocked
    t.expectEqual(@src(), main.wake(9_999_999), abi.EINVAL); // unknown
    t.expectEqual(@src(), main.remove(pid), 0);
    t.expectEqual(@src(), process_if.destroy(pid), 0);
    return t.result();
}

// --- 10. run() is not reentrant -----------------------

var g_nr_rc: c_int = 999;

fn nrEntry(arg: usize) callconv(.c) void {
    _ = arg;
    g_nr_rc = main.run();
    main.exitCurrent();
}

fn testNestedRunIsEbusy() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    g_nr_rc = 999;
    const pid = spawnAdmit(&t, "nr", @intFromPtr(&nrEntry), 0);
    t.expectEqual(@src(), main.run(), 0);
    t.expectEqual(@src(), g_nr_rc, abi.EBUSY);
    t.expectEqual(@src(), process_if.destroy(pid), 0);
    return t.result();
}

// --- 11. many tasks, full rotation -------------------

var g_mt_total: u32 = 0;

fn mtEntry(arg: usize) callconv(.c) void {
    _ = arg;
    var r: u32 = 0;
    while (r < 3) : (r += 1) {
        g_mt_total += 1;
        main.yield();
    }
    main.exitCurrent();
}

fn testManyTasksFullRotation() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const N = 20;
    g_mt_total = 0;
    var pids: [N]u32 = undefined;
    for (0..N) |i| pids[i] = spawnAdmit(&t, "mt", @intFromPtr(&mtEntry), 0);
    t.expectEqual(@src(), main.queueLen(), @as(u32, N));

    t.expectEqual(@src(), main.run(), 0);
    t.expectEqual(@src(), g_mt_total, @as(u32, N * 3));
    t.expectEqual(@src(), main.queueLen(), 0);

    for (pids) |p| t.expectEqual(@src(), process_if.destroy(p), 0);
    return t.result();
}

comptime {
    abi.kernelTest("run_empty_returns", &testRunEmptyReturns);
    abi.kernelTest("single_task_runs_and_exits", &testSingleTaskRunsAndExits);
    abi.kernelTest("round_robin_order", &testRoundRobinOrder);
    abi.kernelTest("yield_single_task_noop", &testYieldSingleTaskNoop);
    abi.kernelTest("block_and_wake", &testBlockAndWake);
    abi.kernelTest("exit_removes_from_rotation", &testExitRemovesFromRotation);
    abi.kernelTest("admit_validation", &testAdmitValidation);
    abi.kernelTest("remove_dequeues", &testRemoveDequeues);
    abi.kernelTest("wake_validation", &testWakeValidation);
    abi.kernelTest("nested_run_is_ebusy", &testNestedRunIsEbusy);
    abi.kernelTest("many_tasks_full_rotation", &testManyTasksFullRotation);
}
