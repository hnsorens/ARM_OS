//! kernelTests for the TCB table. Besides the plain lifecycle/bookkeeping
//! checks, `created_thread_runs` is a real integration test: it takes the
//! `TaskContext` the process module built and drives it with the
//! context_switch module, proving a freshly created thread actually
//! begins executing its entry point on its own kernel stack.
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const main = @import("main.zig");

const serial_if = main.serial_if;
const pmm_if = main.pmm_if;
const cs_if = main.cs_if;

fn mkThread(t: *kernel_test.Tracker, name: [*:0]const u8, entry: usize, pages: u32, prio: u32) u32 {
    var pid: u32 = 0;
    t.expectEqual(@src(), main.createKernelThread(name, entry, 0, pages, prio, &pid), 0);
    return pid;
}

fn idleEntry(arg: usize) callconv(.c) void {
    _ = arg;
    while (true) asm volatile ("wfe");
}

// --- 1. create / query / destroy ---------------------------------

fn testCreateQueryDestroy() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const before = main.count();

    const pid = mkThread(&t, "worker1", @intFromPtr(&idleEntry), 4, 9);
    t.expectNotEqual(@src(), pid, 0);
    t.expectTrue(@src(), main.exists(pid));
    t.expectEqual(@src(), main.count(), before + 1);

    var st: abi.ProcessState = .dead;
    t.expectEqual(@src(), main.getState(pid, &st), 0);
    t.expectEqual(@src(), st, abi.ProcessState.ready);

    var prio: u32 = 0;
    t.expectEqual(@src(), main.getPriority(pid, &prio), 0);
    t.expectEqual(@src(), prio, 9);

    var info: abi.ProcessInfo = .{};
    t.expectEqual(@src(), main.getInfo(pid, &info), 0);
    t.expectEqual(@src(), info.pid, pid);
    t.expectEqual(@src(), info.address_space, 0); // kernel thread
    t.expectTrue(@src(), info.kstack_size >= 4 * 4096);
    t.expectTrue(@src(), info.name[0] == 'w' and info.name[6] == '1');

    var ctx: ?*anyopaque = null;
    t.expectEqual(@src(), main.contextOf(pid, &ctx), 0);
    t.expectTrue(@src(), ctx != null);

    t.expectEqual(@src(), main.destroy(pid), 0);
    t.expectFalse(@src(), main.exists(pid));
    t.expectEqual(@src(), main.count(), before);
    return t.result();
}

// --- 2. a created thread actually runs (context_switch integration)

var g_run_count: u32 = 0;
var g_runner_ctx: ?*anyopaque = null;
var g_main_ctx: abi.TaskContext = .{};

fn runnerEntry(arg: usize) callconv(.c) void {
    _ = arg;
    const myctx: *abi.TaskContext = @ptrCast(@alignCast(g_runner_ctx.?));
    while (true) {
        g_run_count += 1;
        cs_if.switch_to(myctx, &g_main_ctx);
    }
}

fn testCreatedThreadRuns() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    g_run_count = 0;
    g_main_ctx = .{};

    const pid = mkThread(&t, "runner", @intFromPtr(&runnerEntry), 8, 0);
    t.expectEqual(@src(), main.contextOf(pid, &g_runner_ctx), 0);

    const rctx: *abi.TaskContext = @ptrCast(@alignCast(g_runner_ctx.?));
    cs_if.switch_to(&g_main_ctx, rctx);
    t.expectEqual(@src(), g_run_count, 1);
    cs_if.switch_to(&g_main_ctx, rctx);
    cs_if.switch_to(&g_main_ctx, rctx);
    t.expectEqual(@src(), g_run_count, 3);

    // Thread is parked mid-switch on its kernel stack; nothing runs it
    // again, so freeing the stack here is safe.
    t.expectEqual(@src(), main.destroy(pid), 0);
    return t.result();
}

// --- 3. table fills, pids are unique -----------------------------

fn testTableFillsPidsUnique() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.count(), 0);

    var pids: [main.MAX_PROCESSES]u32 = undefined;
    for (0..main.MAX_PROCESSES) |i| {
        pids[i] = mkThread(&t, "fill", @intFromPtr(&idleEntry), 2, 0);
        t.expectNotEqual(@src(), pids[i], 0);
    }
    t.expectEqual(@src(), main.count(), @as(u32, main.MAX_PROCESSES));

    var overflow_pid: u32 = 0;
    t.expectEqual(@src(), main.createKernelThread("nope", @intFromPtr(&idleEntry), 0, 2, 0, &overflow_pid), abi.ENOMEM);

    for (0..main.MAX_PROCESSES) |i| {
        for (i + 1..main.MAX_PROCESSES) |j| {
            t.expectNotEqual(@src(), pids[i], pids[j]);
        }
    }

    for (0..main.MAX_PROCESSES) |i| {
        t.expectEqual(@src(), main.destroy(pids[i]), 0);
    }
    t.expectEqual(@src(), main.count(), 0);
    return t.result();
}

// --- 4. kernel stacks don't overlap ----------------------------

fn testDistinctKernelStacks() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const N = 6;
    var pids: [N]u32 = undefined;
    var bases: [N]u64 = undefined;
    var ends: [N]u64 = undefined;

    for (0..N) |i| {
        pids[i] = mkThread(&t, "stk", @intFromPtr(&idleEntry), 4, 0);
        var info: abi.ProcessInfo = .{};
        t.expectEqual(@src(), main.getInfo(pids[i], &info), 0);
        bases[i] = info.kstack_base;
        ends[i] = info.kstack_base + info.kstack_size;
        t.expectNotEqual(@src(), bases[i], 0);
    }

    for (0..N) |i| {
        for (i + 1..N) |j| {
            const overlap = bases[i] < ends[j] and bases[j] < ends[i];
            t.expectFalse(@src(), overlap);
        }
    }

    for (0..N) |i| t.expectEqual(@src(), main.destroy(pids[i]), 0);
    return t.result();
}

// --- 5. destroy returns the kernel stack to pmm ----------------

fn testStackFreedOnDestroy() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const free_before = pmm_if.get_free_memory();

    const pid = mkThread(&t, "leaky", @intFromPtr(&idleEntry), 8, 0);
    t.expectLessThan(@src(), pmm_if.get_free_memory(), free_before);

    t.expectEqual(@src(), main.destroy(pid), 0);
    // Buddy allocator coalesces on free, so the count must come back
    // exactly, not just approximately.
    t.expectEqual(@src(), pmm_if.get_free_memory(), free_before);
    return t.result();
}

// --- 6. state transitions + validation ------------------------

fn testStateMachine() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const pid = mkThread(&t, "sm", @intFromPtr(&idleEntry), 2, 0);

    var st: abi.ProcessState = .dead;
    t.expectEqual(@src(), main.setState(pid, .blocked), 0);
    t.expectEqual(@src(), main.getState(pid, &st), 0);
    t.expectEqual(@src(), st, abi.ProcessState.blocked);

    t.expectEqual(@src(), main.setState(pid, .running), 0);
    t.expectEqual(@src(), main.setState(pid, .ready), 0);
    t.expectEqual(@src(), main.getState(pid, &st), 0);
    t.expectEqual(@src(), st, abi.ProcessState.ready);

    // .dead is destroy's job, not a settable state; out-of-range rejected.
    t.expectEqual(@src(), main.setState(pid, .dead), abi.EINVAL);
    t.expectEqual(@src(), main.setState(pid, @enumFromInt(99)), abi.EINVAL);

    // unknown pid
    t.expectEqual(@src(), main.setState(9_999_999, .ready), abi.EINVAL);
    t.expectEqual(@src(), main.getState(9_999_999, &st), abi.EINVAL);

    t.expectEqual(@src(), main.destroy(pid), 0);
    return t.result();
}

// --- 7. can't destroy a running thread -----------------------

fn testDestroyRunningIsEbusy() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const pid = mkThread(&t, "run", @intFromPtr(&idleEntry), 2, 0);

    t.expectEqual(@src(), main.setState(pid, .running), 0);
    t.expectEqual(@src(), main.destroy(pid), abi.EBUSY);
    t.expectTrue(@src(), main.exists(pid));

    t.expectEqual(@src(), main.setState(pid, .zombie), 0);
    t.expectEqual(@src(), main.destroy(pid), 0);
    return t.result();
}

// --- 8. priority + exit code round-trips ---------------------

fn testPriorityAndExitCode() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const pid = mkThread(&t, "pex", @intFromPtr(&idleEntry), 2, 1);

    t.expectEqual(@src(), main.setPriority(pid, 7), 0);
    var prio: u32 = 0;
    t.expectEqual(@src(), main.getPriority(pid, &prio), 0);
    t.expectEqual(@src(), prio, 7);

    t.expectEqual(@src(), main.setExitCode(pid, -3), 0);
    var info: abi.ProcessInfo = .{};
    t.expectEqual(@src(), main.getInfo(pid, &info), 0);
    t.expectEqual(@src(), info.exit_code, -3);

    t.expectEqual(@src(), main.setPriority(42, 1), abi.EINVAL);
    t.expectEqual(@src(), main.setExitCode(42, 1), abi.EINVAL);

    t.expectEqual(@src(), main.destroy(pid), 0);
    return t.result();
}

// --- 9. create argument validation --------------------------

fn testCreateRejectsBadArgs() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    var pid: u32 = 0;
    t.expectEqual(@src(), main.createKernelThread("x", 0, 0, 4, 0, &pid), abi.EINVAL); // null entry
    t.expectEqual(@src(), main.createKernelThread("x", @intFromPtr(&idleEntry), 0, 0, 0, &pid), abi.EINVAL); // 0 pages
    t.expectEqual(@src(), main.createKernelThread("x", @intFromPtr(&idleEntry), 0, main.KSTACK_MAX_PAGES + 1, 0, &pid), abi.EINVAL); // too big
    t.expectEqual(@src(), main.count(), 0);
    return t.result();
}

// --- 10. a freed slot is reusable --------------------------

fn testSlotReusedAfterDestroy() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const first = mkThread(&t, "a", @intFromPtr(&idleEntry), 2, 0);
    t.expectEqual(@src(), main.destroy(first), 0);

    const second = mkThread(&t, "b", @intFromPtr(&idleEntry), 2, 0);
    // Slot reused, but pids are monotonic -- not recycled.
    t.expectNotEqual(@src(), second, first);
    t.expectTrue(@src(), main.exists(second));
    t.expectFalse(@src(), main.exists(first));

    t.expectEqual(@src(), main.destroy(second), 0);
    return t.result();
}

// --- 11. list() enumerates live pids -----------------------

fn testListEnumeratesLivePids() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const a = mkThread(&t, "a", @intFromPtr(&idleEntry), 2, 0);
    const b = mkThread(&t, "b", @intFromPtr(&idleEntry), 2, 0);
    const c = mkThread(&t, "c", @intFromPtr(&idleEntry), 2, 0);

    var buf: [main.MAX_PROCESSES]u32 = undefined;
    var n: u32 = 0;
    t.expectEqual(@src(), main.list(&buf, buf.len, &n), 0);
    t.expectEqual(@src(), n, 3);

    var seen_a = false;
    var seen_b = false;
    var seen_c = false;
    for (buf[0..n]) |p| {
        if (p == a) seen_a = true;
        if (p == b) seen_b = true;
        if (p == c) seen_c = true;
    }
    t.expectTrue(@src(), seen_a and seen_b and seen_c);

    t.expectEqual(@src(), main.destroy(b), 0);
    t.expectEqual(@src(), main.list(&buf, buf.len, &n), 0);
    t.expectEqual(@src(), n, 2);

    // A too-small buffer reports EOVERFLOW but still fills what it can.
    var small: [1]u32 = undefined;
    var m: u32 = 0;
    t.expectEqual(@src(), main.list(&small, 1, &m), abi.EOVERFLOW);
    t.expectEqual(@src(), m, 1);

    t.expectEqual(@src(), main.destroy(a), 0);
    t.expectEqual(@src(), main.destroy(c), 0);
    return t.result();
}

// --- 12. list()/count() must not copy the TCB table onto the stack -----
//
// Regression: `for (s_table) |t|` iterated the 64-entry table *by value*,
// so each call spilled a ~48 KiB copy of the whole table as a loop temp.
// `list()` runs from `sysWait4` on an 8-page (32 KiB) syscall stack, so
// the copy overflowed the stack straight into whatever the pmm had put
// physically below it -- for /forktest that was the parent's own L1 page
// table, which got zeroed, faulting the parent the instant it called
// wait4() after fork(). This test drives both functions on a dedicated
// stack painted with a sentinel and asserts they touch < 4 KiB of it.

const STACK_PAINT: u64 = 0xA5A5A5A5A5A5A5A5;

var g_su_ctx: ?*anyopaque = null;
var g_su_main: abi.TaskContext = .{};
var g_su_base: u64 = 0;
var g_su_top: u64 = 0;
var g_su_used: u64 = 0;
var g_su_list_rc: c_int = 1;
var g_su_n: u32 = 0;

fn stackUseRunner(arg: usize) callconv(.c) void {
    _ = arg;
    const sp = asm volatile ("mov %[o], sp"
        : [o] "=r" (-> u64),
    );
    // Paint the free span between the stack base and our own frame, run
    // the functions under test, then find how far down the paint was
    // trampled.
    const lo = (g_su_base + 256 + 7) & ~@as(u64, 7);
    const hi = (sp - 512) & ~@as(u64, 7);
    var p = lo;
    while (p < hi) : (p += 8) @as(*volatile u64, @ptrFromInt(p)).* = STACK_PAINT;

    var buf: [main.MAX_PROCESSES]u32 = undefined;
    var i: u32 = 0;
    while (i < 16) : (i += 1) {
        _ = main.count();
        g_su_list_rc = main.list(&buf, buf.len, &g_su_n);
    }

    var deepest = hi;
    p = lo;
    while (p < hi) : (p += 8) {
        if (@as(*volatile u64, @ptrFromInt(p)).* != STACK_PAINT) {
            deepest = p;
            break;
        }
    }
    g_su_used = g_su_top - deepest;
    cs_if.switch_to(@ptrCast(@alignCast(g_su_ctx.?)), &g_su_main);
    while (true) {}
}

fn testListDoesNotCopyTableToStack() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    g_su_main = .{};
    g_su_used = 0;
    g_su_list_rc = 1;

    const filler = mkThread(&t, "su", @intFromPtr(&idleEntry), 2, 0);

    // 64-page (256 KiB) stack: big enough that even the ~48 KiB buggy
    // copy stays inside it -- so an unfixed build gets a clean FAIL here
    // rather than corrupting the kernel and taking the whole run down.
    const pid = mkThread(&t, "stackuse", @intFromPtr(&stackUseRunner), 64, 0);
    t.expectEqual(@src(), main.contextOf(pid, &g_su_ctx), 0);

    var info: abi.ProcessInfo = .{};
    t.expectEqual(@src(), main.getInfo(pid, &info), 0);
    g_su_base = info.kstack_base;
    g_su_top = info.kstack_base + info.kstack_size;

    const rctx: *abi.TaskContext = @ptrCast(@alignCast(g_su_ctx.?));
    cs_if.switch_to(&g_su_main, rctx);

    t.expectEqual(@src(), g_su_list_rc, 0);
    t.expectEqual(@src(), g_su_n, main.count());
    // Fixed: both functions touch a few hundred bytes. Buggy: ~48 KiB.
    t.expectTrue(@src(), g_su_used < 4096);

    t.expectEqual(@src(), main.destroy(pid), 0);
    t.expectEqual(@src(), main.destroy(filler), 0);
    return t.result();
}

// --- forked-process TCB bookkeeping (no EL0 run) -----------------

fn testForkedProcessTcb() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const std = @import("std");

    var frame = std.mem.zeroes(abi.TrapFrame);
    frame.elr = 0x1000_0000_0000;
    frame.x[0] = 0xAAAA; // must be forced to 0 in the child's copy

    const fake_ttbr0: u64 = (@as(u64, 5) << 48) | 0xDEAD000;
    var pid: u32 = 0;
    t.expectEqual(@src(), main.createForkedProcess("f", fake_ttbr0, 42, &frame, 4, 3, &pid), 0);
    t.expectNotEqual(@src(), pid, 0);

    var info: abi.ProcessInfo = .{};
    t.expectEqual(@src(), main.getInfo(pid, &info), 0);
    t.expectEqual(@src(), info.parent, 42);
    t.expectTrue(@src(), info.is_user);
    t.expectEqual(@src(), info.state, abi.ProcessState.ready);
    t.expectEqual(@src(), info.address_space, fake_ttbr0);
    t.expectEqual(@src(), info.priority, 3);

    t.expectEqual(@src(), main.setParent(pid, 99), 0);
    t.expectEqual(@src(), main.setAddressSpace(pid, 0x1234000), 0);
    t.expectEqual(@src(), main.getInfo(pid, &info), 0);
    t.expectEqual(@src(), info.parent, 99);
    t.expectEqual(@src(), info.address_space, 0x1234000);

    // Bad-arg guards.
    var bad: u32 = 0;
    t.expectNotEqual(@src(), main.createForkedProcess("x", 0, 1, &frame, 4, 0, &bad), 0); // null ttbr0
    t.expectEqual(@src(), main.setParent(9_999_999, 1), abi.EINVAL);
    t.expectEqual(@src(), main.setAddressSpace(9_999_999, 1), abi.EINVAL);

    t.expectEqual(@src(), main.destroy(pid), 0);
    return t.result();
}

comptime {
    abi.kernelTest("forked_process_tcb", &testForkedProcessTcb);
    abi.kernelTest("create_query_destroy", &testCreateQueryDestroy);
    abi.kernelTest("created_thread_runs", &testCreatedThreadRuns);
    abi.kernelTest("table_fills_pids_unique", &testTableFillsPidsUnique);
    abi.kernelTest("distinct_kernel_stacks", &testDistinctKernelStacks);
    abi.kernelTest("stack_freed_on_destroy", &testStackFreedOnDestroy);
    abi.kernelTest("state_machine", &testStateMachine);
    abi.kernelTest("destroy_running_is_ebusy", &testDestroyRunningIsEbusy);
    abi.kernelTest("priority_and_exit_code", &testPriorityAndExitCode);
    abi.kernelTest("create_rejects_bad_args", &testCreateRejectsBadArgs);
    abi.kernelTest("slot_reused_after_destroy", &testSlotReusedAfterDestroy);
    abi.kernelTest("list_enumerates_live_pids", &testListEnumeratesLivePids);
    abi.kernelTest("list_does_not_copy_table_to_stack", &testListDoesNotCopyTableToStack);
}
