//! kernelTests for the syscall table. `invoke` exercises the dispatch
//! path directly; the `svc_*` tests fire a real `svc #0` from EL1 so the
//! full chain -- exceptions vector -> `.sync_svc` callback -> table
//! dispatch -> x0 writeback -> `eret` -- is covered end to end, including
//! the AArch64 register convention (number in x8, args x0..x5, result in
//! x0).
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const main = @import("main.zig");

const serial_if = main.serial_if;

const ENOSYS_U64: u64 = @bitCast(@as(i64, -@as(i64, abi.ENOSYS)));

// --- handlers ----------------------------------------------------

fn sumHandler(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    var s: i64 = 0;
    for (args.arg) |a| s += @intCast(a);
    return s;
}

fn weightedHandler(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    var s: i64 = 0;
    for (args.arg, 1..) |a, w| s += @as(i64, @intCast(a)) * @as(i64, @intCast(w));
    return s;
}

fn ctxHandler(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = args;
    return @intCast(@intFromPtr(ctx));
}

fn negHandler(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = args;
    _ = ctx;
    return -22;
}

fn twoArgMul(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    return @as(i64, @intCast(args.arg[0])) * 10 + @as(i64, @intCast(args.arg[1]));
}

fn const100(_: *const abi.SyscallArgs, _: ?*anyopaque) callconv(.c) i64 {
    return 100;
}
fn const200(_: *const abi.SyscallArgs, _: ?*anyopaque) callconv(.c) i64 {
    return 200;
}

// --- 1. register + invoke round-trip -------------------------

fn testRegisterAndInvoke() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const before = main.count();

    t.expectEqual(@src(), main.register(400, &sumHandler, null), 0);
    t.expectTrue(@src(), main.isRegistered(400));
    t.expectEqual(@src(), main.count(), before + 1);

    t.expectEqual(@src(), main.invoke(400, 1, 2, 3, 4, 5, 6), 21);

    t.expectEqual(@src(), main.unregister(400), 0);
    t.expectFalse(@src(), main.isRegistered(400));
    t.expectEqual(@src(), main.count(), before);
    return t.result();
}

// --- 2. invoking an unregistered number -> -ENOSYS ----------

fn testInvokeUnregisteredEnosys() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectFalse(@src(), main.isRegistered(401));
    t.expectEqual(@src(), main.invoke(401, 0, 0, 0, 0, 0, 0), -@as(i64, abi.ENOSYS));
    // Out of range too.
    t.expectEqual(@src(), main.invoke(abi.SYSCALL_TABLE_SIZE + 5, 0, 0, 0, 0, 0, 0), -@as(i64, abi.ENOSYS));
    return t.result();
}

// --- 3. svc path dispatches to the handler -----------------

fn testSvcPathDispatches() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    // twoArgMul reads only arg[0]/arg[1], so leftover junk in x2..x5 is
    // irrelevant here (see all_six_args_pass_through for the full set).
    t.expectEqual(@src(), main.register(402, &twoArgMul, null), 0);

    // nr=402 in x8; args 7 and 3 in x0,x1 -> 7*10 + 3 = 73.
    const r = asm volatile (
        \\ mov x8, #402
        \\ mov x0, #7
        \\ mov x1, #3
        \\ svc #0
        : [ret] "={x0}" (-> u64),
        :
        : .{ .x8 = true, .x1 = true, .memory = true });
    t.expectEqual(@src(), r, 73);

    t.expectEqual(@src(), main.unregister(402), 0);
    return t.result();
}

// --- 4. svc for an unregistered number -> -ENOSYS in x0 ----

fn testSvcUnregisteredEnosys() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectFalse(@src(), main.isRegistered(403));

    const r = asm volatile (
        \\ mov x8, #403
        \\ svc #0
        : [ret] "={x0}" (-> u64),
        :
        : .{ .x8 = true, .memory = true });
    t.expectEqual(@src(), r, ENOSYS_U64);
    return t.result();
}

// --- 5. registration validation ---------------------------

fn testRegisterValidation() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.register(abi.SYSCALL_TABLE_SIZE, &sumHandler, null), abi.EINVAL);
    t.expectEqual(@src(), main.register(404, null, null), abi.EINVAL);

    t.expectEqual(@src(), main.register(404, &sumHandler, null), 0);
    t.expectEqual(@src(), main.register(404, &const100, null), abi.EBUSY);
    t.expectEqual(@src(), main.unregister(404), 0);
    // Idempotent unregister; out-of-range unregister is EINVAL.
    t.expectEqual(@src(), main.unregister(404), 0);
    t.expectEqual(@src(), main.unregister(abi.SYSCALL_TABLE_SIZE + 1), abi.EINVAL);

    // Slot reusable after unregister.
    t.expectEqual(@src(), main.register(404, &const200, null), 0);
    t.expectEqual(@src(), main.invoke(404, 0, 0, 0, 0, 0, 0), 200);
    t.expectEqual(@src(), main.unregister(404), 0);
    return t.result();
}

// --- 6. all six args pass through (invoke + svc) ----------

fn testAllSixArgsPassThrough() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.register(405, &weightedHandler, null), 0);

    // 1*1 + 2*2 + 3*3 + 4*4 + 5*5 + 6*6 = 91
    t.expectEqual(@src(), main.invoke(405, 1, 2, 3, 4, 5, 6), 91);

    const r = asm volatile (
        \\ mov x8, #405
        \\ mov x0, #1
        \\ mov x1, #2
        \\ mov x2, #3
        \\ mov x3, #4
        \\ mov x4, #5
        \\ mov x5, #6
        \\ svc #0
        : [ret] "={x0}" (-> u64),
        :
        : .{ .x8 = true, .x1 = true, .x2 = true, .x3 = true, .x4 = true, .x5 = true, .memory = true });
    t.expectEqual(@src(), r, 91);

    t.expectEqual(@src(), main.unregister(405), 0);
    return t.result();
}

// --- 7. the ctx pointer reaches the handler --------------

fn testHandlerCtxPassed() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const token: *anyopaque = @ptrFromInt(0x5150);
    t.expectEqual(@src(), main.register(406, &ctxHandler, token), 0);
    t.expectEqual(@src(), main.invoke(406, 0, 0, 0, 0, 0, 0), 0x5150);
    t.expectEqual(@src(), main.unregister(406), 0);
    return t.result();
}

// --- 8. a negative return survives the round trip -------

fn testNegativeReturnRoundTrip() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.register(407, &negHandler, null), 0);

    t.expectEqual(@src(), main.invoke(407, 0, 0, 0, 0, 0, 0), -22);

    const r = asm volatile (
        \\ mov x8, #407
        \\ svc #0
        : [ret] "={x0}" (-> u64),
        :
        : .{ .x8 = true, .memory = true });
    t.expectEqual(@src(), r, @as(u64, @bitCast(@as(i64, -22))));

    t.expectEqual(@src(), main.unregister(407), 0);
    return t.result();
}

// --- 9. two numbers are independent --------------------

fn testTwoNumbersIndependent() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.register(408, &const100, null), 0);
    t.expectEqual(@src(), main.register(409, &const200, null), 0);

    t.expectEqual(@src(), main.invoke(408, 0, 0, 0, 0, 0, 0), 100);
    t.expectEqual(@src(), main.invoke(409, 0, 0, 0, 0, 0, 0), 200);

    t.expectEqual(@src(), main.unregister(408), 0);
    t.expectEqual(@src(), main.invoke(408, 0, 0, 0, 0, 0, 0), -@as(i64, abi.ENOSYS));
    t.expectEqual(@src(), main.invoke(409, 0, 0, 0, 0, 0, 0), 200);

    t.expectEqual(@src(), main.unregister(409), 0);
    return t.result();
}

// --- 10. count tracks registrations -------------------

fn testCountTracksRegistrations() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const base = main.count();

    t.expectEqual(@src(), main.register(410, &const100, null), 0);
    t.expectEqual(@src(), main.register(411, &const100, null), 0);
    t.expectEqual(@src(), main.register(412, &const100, null), 0);
    t.expectEqual(@src(), main.count(), base + 3);

    t.expectEqual(@src(), main.unregister(410), 0);
    t.expectEqual(@src(), main.unregister(411), 0);
    t.expectEqual(@src(), main.unregister(412), 0);
    t.expectEqual(@src(), main.count(), base);
    return t.result();
}

// --- 11. a raw handler sees the trap frame + wins over a plain one ---

var g_raw_nr: u64 = 0;
var g_raw_x0: u64 = 0;

fn rawHandler(frame: *abi.TrapFrame, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    g_raw_nr = frame.x[8];
    g_raw_x0 = frame.x[0];
    return @as(i64, @intCast(frame.x[0])) * 2; // return -> x0 on eret
}

fn testRawHandlerSeesFrame() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    g_raw_nr = 0;
    g_raw_x0 = 0;
    t.expectEqual(@src(), main.registerRaw(420, &rawHandler, null), 0);
    t.expectEqual(@src(), main.register(420, &sumHandler, null), 0); // plain also set

    const ret = asm volatile (
        \\ mov x8, #420
        \\ mov x0, #21
        \\ svc #0
        : [r] "={x0}" (-> u64),
        :
        : .{ .x8 = true, .memory = true });

    t.expectEqual(@src(), g_raw_nr, 420); // raw handler ran (not the plain sum)
    t.expectEqual(@src(), g_raw_x0, 21); // it saw the frame's x0
    t.expectEqual(@src(), ret, 42); // its return (21*2) reached x0

    t.expectEqual(@src(), main.unregisterRaw(420), 0);
    t.expectEqual(@src(), main.unregister(420), 0);
    return t.result();
}

comptime {
    abi.kernelTest("raw_handler_sees_frame", &testRawHandlerSeesFrame);
    abi.kernelTest("register_and_invoke", &testRegisterAndInvoke);
    abi.kernelTest("invoke_unregistered_enosys", &testInvokeUnregisteredEnosys);
    abi.kernelTest("svc_path_dispatches", &testSvcPathDispatches);
    abi.kernelTest("svc_unregistered_enosys", &testSvcUnregisteredEnosys);
    abi.kernelTest("register_validation", &testRegisterValidation);
    abi.kernelTest("all_six_args_pass_through", &testAllSixArgsPassThrough);
    abi.kernelTest("handler_ctx_passed", &testHandlerCtxPassed);
    abi.kernelTest("negative_return_round_trip", &testNegativeReturnRoundTrip);
    abi.kernelTest("two_numbers_independent", &testTwoNumbersIndependent);
    abi.kernelTest("count_tracks_registrations", &testCountTracksRegistrations);
}
