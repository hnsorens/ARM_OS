//! kernelTests for the cooperative context switcher. Every test drives
//! real stack/register switches on the live EL1 CPU: a set of static
//! per-"task" stacks, `TaskContext`s built by `initKernelContext`, and
//! `ctxsw_switch_to` ping-ponging between the test function's own context
//! and the workers'. Interrupts stay enabled throughout (the timer module
//! is also live) -- the switch primitives are written to tolerate an IRQ
//! landing anywhere in them.
const std = @import("std");
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const main = @import("main.zig");

const serial_if = main.serial_if;

extern fn ctxsw_switch_to(save: *abi.TaskContext, restore: *const abi.TaskContext) callconv(.c) void;
extern fn ctxsw_jump_to(restore: *const abi.TaskContext) callconv(.c) noreturn;

const STACK_BYTES = 32 * 1024;
const NUM_STACKS = 4;

var g_stacks: [NUM_STACKS][STACK_BYTES]u8 align(16) = undefined;

fn stackTop(i: usize) u64 {
    return @intFromPtr(&g_stacks[i]) + STACK_BYTES;
}

// Context the test function parks its own execution in while a worker
// runs; workers switch back into this to return control to the test.
var g_main_ctx: abi.TaskContext = .{};

// --- 1. one yield out and back --------------------------------------

var g_one_ctx: abi.TaskContext = .{};
var g_one_counter: u64 = 0;

fn oneShotWorker(arg: usize) callconv(.c) void {
    _ = arg;
    while (true) {
        g_one_counter += 1;
        ctxsw_switch_to(&g_one_ctx, &g_main_ctx);
    }
}

fn testSingleYieldRoundTrip() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    g_one_counter = 0;
    g_main_ctx = .{};
    t.expectEqual(@src(), main.initKernelContext(&g_one_ctx, @intFromPtr(&oneShotWorker), 0, stackTop(0)), 0);

    ctxsw_switch_to(&g_main_ctx, &g_one_ctx);
    // Control is back in the test, on the test's stack, after exactly one
    // pass through the worker loop body.
    t.expectEqual(@src(), g_one_counter, 1);

    ctxsw_switch_to(&g_main_ctx, &g_one_ctx);
    t.expectEqual(@src(), g_one_counter, 2);
    return t.result();
}

// --- 2. many ping-pongs, and a canary that must survive them --------

var g_pp_ctx: abi.TaskContext = .{};
var g_pp_counter: u64 = 0;

fn pingPongWorker(arg: usize) callconv(.c) void {
    _ = arg;
    while (true) {
        g_pp_counter += 1;
        ctxsw_switch_to(&g_pp_ctx, &g_main_ctx);
    }
}

fn testPingPongMany() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const N: u64 = 4000;
    g_pp_counter = 0;
    g_main_ctx = .{};
    t.expectEqual(@src(), main.initKernelContext(&g_pp_ctx, @intFromPtr(&pingPongWorker), 0, stackTop(0)), 0);

    // `canary` is a value the compiler keeps live across the switch_to
    // call (it's used afterward), i.e. in a callee-saved register or a
    // test-stack slot -- either way, switch_to must not corrupt it.
    var canary: u64 = 0xCAFEF00DDEADBEEF;
    var i: u64 = 0;
    while (i < N) : (i += 1) {
        ctxsw_switch_to(&g_main_ctx, &g_pp_ctx);
        canary ^= i;
    }

    var expected_canary: u64 = 0xCAFEF00DDEADBEEF;
    i = 0;
    while (i < N) : (i += 1) expected_canary ^= i;

    t.expectEqual(@src(), g_pp_counter, N);
    t.expectEqual(@src(), canary, expected_canary);
    return t.result();
}

// --- 3. three workers handing off around a ring --------------------

var g_ring_ctx: [3]abi.TaskContext = .{ .{}, .{}, .{} };
var g_ring_trace: [64]u8 = undefined;
var g_ring_len: usize = 0;
var g_ring_laps: u32 = 0;

fn ringPush(c: u8) void {
    if (g_ring_len < g_ring_trace.len) {
        g_ring_trace[g_ring_len] = c;
        g_ring_len += 1;
    }
}

fn ringWorker(arg: usize) callconv(.c) void {
    const id: usize = arg;
    const next: usize = (id + 1) % 3;
    while (true) {
        ringPush(@intCast('A' + id));
        if (id == 2) g_ring_laps += 1;
        // Worker 2 closes each lap by handing control back to the test;
        // workers 0 and 1 hand off to their ring neighbour.
        if (id == 2) {
            ctxsw_switch_to(&g_ring_ctx[id], &g_main_ctx);
        } else {
            ctxsw_switch_to(&g_ring_ctx[id], &g_ring_ctx[next]);
        }
    }
}

fn testThreeContextRoundRobin() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    g_ring_len = 0;
    g_ring_laps = 0;
    g_main_ctx = .{};
    for (0..3) |i| {
        g_ring_ctx[i] = .{};
        t.expectEqual(@src(), main.initKernelContext(&g_ring_ctx[i], @intFromPtr(&ringWorker), i, stackTop(i)), 0);
    }

    const LAPS: u32 = 5;
    var lap: u32 = 0;
    while (lap < LAPS) : (lap += 1) {
        ctxsw_switch_to(&g_main_ctx, &g_ring_ctx[0]);
    }

    t.expectEqual(@src(), g_ring_laps, LAPS);
    t.expectEqual(@src(), g_ring_len, @as(usize, LAPS * 3));
    // Trace must be "ABC" repeated once per lap.
    var ok = true;
    for (0..g_ring_len) |i| {
        const want: u8 = @intCast('A' + (i % 3));
        if (g_ring_trace[i] != want) ok = false;
    }
    t.expectTrue(@src(), ok);
    return t.result();
}

// --- 4. each worker's stack is private and survives the other running

var g_iso_a_ctx: abi.TaskContext = .{};
var g_iso_b_ctx: abi.TaskContext = .{};
var g_iso_a_ok: bool = false;
var g_iso_b_ok: bool = false;

fn isoWorkerA(arg: usize) callconv(.c) void {
    _ = arg;
    var buf: [2048]u8 = undefined;
    for (&buf) |*b| b.* = 0xAA;
    ctxsw_switch_to(&g_iso_a_ctx, &g_main_ctx);
    var ok = true;
    for (buf) |b| {
        if (b != 0xAA) ok = false;
    }
    g_iso_a_ok = ok;
    ctxsw_switch_to(&g_iso_a_ctx, &g_main_ctx);
}

fn isoWorkerB(arg: usize) callconv(.c) void {
    _ = arg;
    var buf: [2048]u8 = undefined;
    for (&buf) |*b| b.* = 0x55;
    ctxsw_switch_to(&g_iso_b_ctx, &g_main_ctx);
    var ok = true;
    for (buf) |b| {
        if (b != 0x55) ok = false;
    }
    g_iso_b_ok = ok;
    ctxsw_switch_to(&g_iso_b_ctx, &g_main_ctx);
}

fn testSeparateStacksAreIsolated() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    g_iso_a_ok = false;
    g_iso_b_ok = false;
    g_main_ctx = .{};
    g_iso_a_ctx = .{};
    g_iso_b_ctx = .{};
    t.expectEqual(@src(), main.initKernelContext(&g_iso_a_ctx, @intFromPtr(&isoWorkerA), 0, stackTop(0)), 0);
    t.expectEqual(@src(), main.initKernelContext(&g_iso_b_ctx, @intFromPtr(&isoWorkerB), 0, stackTop(1)), 0);

    ctxsw_switch_to(&g_main_ctx, &g_iso_a_ctx); // A fills its stack buffer
    ctxsw_switch_to(&g_main_ctx, &g_iso_b_ctx); // B fills a different stack
    ctxsw_switch_to(&g_main_ctx, &g_iso_a_ctx); // A re-checks its buffer
    ctxsw_switch_to(&g_main_ctx, &g_iso_b_ctx); // B re-checks its buffer

    t.expectTrue(@src(), g_iso_a_ok);
    t.expectTrue(@src(), g_iso_b_ok);
    return t.result();
}

// --- 5. jump_to starts a context with no caller to come back to -----

var g_jt_ctx: abi.TaskContext = .{};
var g_jt_bootstrap_ctx: abi.TaskContext = .{};
var g_jt_trace: [8]u8 = undefined;
var g_jt_len: usize = 0;

fn jtPush(c: u8) void {
    if (g_jt_len < g_jt_trace.len) {
        g_jt_trace[g_jt_len] = c;
        g_jt_len += 1;
    }
}

fn jtSecondary(arg: usize) callconv(.c) void {
    _ = arg;
    jtPush('S');
    // Hand control back to the test's saved context.
    ctxsw_switch_to(&g_jt_ctx, &g_main_ctx);
}

fn jtBootstrap(arg: usize) callconv(.c) void {
    _ = arg;
    jtPush('B');
    // Discard this (bootstrap) context entirely and start the secondary.
    ctxsw_jump_to(&g_jt_ctx);
}

fn testJumpToStartsFresh() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    g_jt_len = 0;
    g_main_ctx = .{};
    g_jt_ctx = .{};
    g_jt_bootstrap_ctx = .{};
    t.expectEqual(@src(), main.initKernelContext(&g_jt_bootstrap_ctx, @intFromPtr(&jtBootstrap), 0, stackTop(0)), 0);
    t.expectEqual(@src(), main.initKernelContext(&g_jt_ctx, @intFromPtr(&jtSecondary), 0, stackTop(1)), 0);

    ctxsw_switch_to(&g_main_ctx, &g_jt_bootstrap_ctx);

    t.expectEqual(@src(), g_jt_len, @as(usize, 2));
    t.expectEqual(@src(), g_jt_trace[0], @as(u8, 'B'));
    t.expectEqual(@src(), g_jt_trace[1], @as(u8, 'S'));
    return t.result();
}

// --- 6. a worker resumes right after its own switch point ----------
//
// Proves lr is saved at the call site (so re-entry continues the worker
// mid-function) rather than reset to the trampoline every time.

var g_sm_ctx: abi.TaskContext = .{};
var g_sm_stage: u32 = 0;

fn stateMachineWorker(arg: usize) callconv(.c) void {
    _ = arg;
    g_sm_stage = 1;
    ctxsw_switch_to(&g_sm_ctx, &g_main_ctx);
    g_sm_stage = 2;
    ctxsw_switch_to(&g_sm_ctx, &g_main_ctx);
    g_sm_stage = 3;
    ctxsw_switch_to(&g_sm_ctx, &g_main_ctx);
    // Never returns off the end.
    while (true) ctxsw_switch_to(&g_sm_ctx, &g_main_ctx);
}

fn testReEntryResumesAfterSwitchPoint() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    g_sm_stage = 0;
    g_main_ctx = .{};
    g_sm_ctx = .{};
    t.expectEqual(@src(), main.initKernelContext(&g_sm_ctx, @intFromPtr(&stateMachineWorker), 0, stackTop(0)), 0);

    ctxsw_switch_to(&g_main_ctx, &g_sm_ctx);
    t.expectEqual(@src(), g_sm_stage, 1);
    ctxsw_switch_to(&g_main_ctx, &g_sm_ctx);
    t.expectEqual(@src(), g_sm_stage, 2);
    ctxsw_switch_to(&g_main_ctx, &g_sm_ctx);
    t.expectEqual(@src(), g_sm_stage, 3);
    return t.result();
}

// --- 7. init_kernel_context argument validation -------------------

fn testInitRejectsBadArgs() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    var ctx: abi.TaskContext = .{};

    t.expectNotEqual(@src(), main.initKernelContext(&ctx, 0, 0, stackTop(0)), 0); // null entry
    t.expectNotEqual(@src(), main.initKernelContext(&ctx, 0x1000, 0, 0), 0); // stack_top 0
    t.expectNotEqual(@src(), main.initKernelContext(&ctx, 0x1000, 0, 100), 0); // stack_top too small

    t.expectEqual(@src(), main.initKernelContext(&ctx, 0x1000, 7, stackTop(0)), 0);
    // sp is normalised down to a 16-byte boundary; lr points at the trampoline.
    t.expectEqual(@src(), ctx.sp & 0xF, 0);
    t.expectNotEqual(@src(), ctx.lr, 0);
    t.expectEqual(@src(), ctx.x19, 0x1000); // entry stashed for the trampoline
    t.expectEqual(@src(), ctx.x20, 7); // arg stashed for the trampoline
    return t.result();
}

// --- 8. callee-saved registers preserved across a switch ----------

var g_cs_ctx: abi.TaskContext = .{};

fn calleeSaveWorker(arg: usize) callconv(.c) void {
    _ = arg;
    // Trample the callee-saved set with junk, then yield. When the test
    // resumes, its own callee-saved values must be the ones restored, not
    // this junk.
    while (true) {
        asm volatile (
            \\ mov x19, #0x1111
            \\ mov x20, #0x2222
            \\ mov x21, #0x3333
            \\ mov x22, #0x4444
            \\ mov x23, #0x5555
            \\ mov x24, #0x6666
            \\ mov x25, #0x7777
            \\ mov x26, #0x8888
            \\ mov x27, #0x9999
            \\ mov x28, #0xAAAA
            ::: .{ .x19 = true, .x20 = true, .x21 = true, .x22 = true, .x23 = true, .x24 = true, .x25 = true, .x26 = true, .x27 = true, .x28 = true });
        ctxsw_switch_to(&g_cs_ctx, &g_main_ctx);
    }
}

fn testCalleeSavedPreservedAcrossSwitch() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    g_main_ctx = .{};
    g_cs_ctx = .{};
    t.expectEqual(@src(), main.initKernelContext(&g_cs_ctx, @intFromPtr(&calleeSaveWorker), 0, stackTop(0)), 0);

    // Sum a stack array in a loop after each switch: the accumulator and
    // loop bounds are exactly the kind of long-lived values the compiler
    // parks in x19-x28 across the switch_to call.
    var data: [256]u32 = undefined;
    for (&data, 0..) |*d, i| d.* = @intCast(i * 3 + 1);

    var acc: u64 = 0;
    var round: u32 = 0;
    while (round < 32) : (round += 1) {
        ctxsw_switch_to(&g_main_ctx, &g_cs_ctx);
        for (data) |d| acc += d;
    }

    var expected: u64 = 0;
    for (data) |d| expected += d;
    expected *= 32;

    t.expectEqual(@src(), acc, expected);
    return t.result();
}

comptime {
    abi.kernelTest("single_yield_round_trip", &testSingleYieldRoundTrip);
    abi.kernelTest("ping_pong_many", &testPingPongMany);
    abi.kernelTest("three_context_round_robin", &testThreeContextRoundRobin);
    abi.kernelTest("separate_stacks_are_isolated", &testSeparateStacksAreIsolated);
    abi.kernelTest("jump_to_starts_fresh", &testJumpToStartsFresh);
    abi.kernelTest("re_entry_resumes_after_switch_point", &testReEntryResumesAfterSwitchPoint);
    abi.kernelTest("init_rejects_bad_args", &testInitRejectsBadArgs);
    abi.kernelTest("callee_saved_preserved_across_switch", &testCalleeSavedPreservedAcrossSwitch);
}
