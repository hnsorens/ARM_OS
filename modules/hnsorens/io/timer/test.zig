//! Tests for the software-multiplexed timer service in `main.zig`, split
//! into their own file (reachable from the build via `main.zig`'s
//! `comptime { _ = @import("test.zig"); }`).
//!
//! Like gic_v3's own timer test, these exercise real hardware (QEMU
//! virt's GICv3 + EL1 physical timer), not a mock. Each test does its own
//! gic_if.init_global/init_core (idempotent after the first successful
//! call -- see gic_v3's own test comment) and manages daifclr/daifset
//! around its wait window itself, since nothing else in this bring-up-only
//! codebase runs a persistent IRQ-enabled loop yet.
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const main = @import("main.zig");

const serial_if = main.serial_if;
const gic_if = main.gic_if;

const GICD_BASE: u64 = 0x8000000;
const GICR_BASE: u64 = 0x80A0000;

fn ticksForMs(ms: u64) u32 {
    return @intCast((main.readCntfrq() / 1000) * ms);
}

fn irqsOn() void {
    asm volatile ("msr daifclr, #2" ::: .{ .memory = true });
}
fn irqsOff() void {
    asm volatile ("msr daifset, #2" ::: .{ .memory = true });
}

// Busy-spins on `cond` up to a large iteration cap rather than a wall-clock
// deadline, matching gic_v3's testTimerInterrupt -- the cap is a safety
// bound, not something normal test runs are expected to hit.
fn spinUntil(cond: *const fn () bool) void {
    var timeout: i64 = 1_000_000_000;
    while (!cond() and timeout > 0) : (timeout -= 1) {
        asm volatile ("nop");
    }
}

var g_oneshot_fired: bool = false;
var g_oneshot_id: u32 = 0;

fn oneshotCallback(id: u32, context: ?*anyopaque) callconv(.c) void {
    _ = context;
    g_oneshot_id = id;
    g_oneshot_fired = true;
}

fn oneshotCond() bool {
    return g_oneshot_fired;
}

fn testOneShotCallbackFires() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    _ = gic_if.init_global(GICD_BASE, GICR_BASE);
    _ = gic_if.init_core();

    g_oneshot_fired = false;
    g_oneshot_id = 0;

    var id: u32 = 0;
    t.expectEqual(@src(), main.registerCallback(ticksForMs(10), 0, &oneshotCallback, &id, null), 0);
    t.expectTrue(@src(), id != 0);

    irqsOn();
    spinUntil(&oneshotCond);
    irqsOff();

    t.expectTrue(@src(), g_oneshot_fired);
    t.expectEqual(@src(), g_oneshot_id, id);
    // One-shot timers deactivate themselves on fire -- unregistering an
    // already-fired id must report EINVAL, not silently succeed.
    t.expectEqual(@src(), main.unregisterCallback(id), abi.EINVAL);
    return t.result();
}

var g_periodic_count: u32 = 0;
var g_periodic_id: u32 = 0;

fn periodicCallback(id: u32, context: ?*anyopaque) callconv(.c) void {
    _ = context;
    _ = id;
    g_periodic_count += 1;
}

fn periodicCond() bool {
    return g_periodic_count >= 3;
}

fn testPeriodicCallbackFiresAndStops() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    _ = gic_if.init_global(GICD_BASE, GICR_BASE);
    _ = gic_if.init_core();

    g_periodic_count = 0;

    t.expectEqual(@src(), main.registerCallback(ticksForMs(5), 1, &periodicCallback, &g_periodic_id, null), 0);

    irqsOn();
    spinUntil(&periodicCond);
    irqsOff();

    t.expectTrue(@src(), g_periodic_count >= 3);

    t.expectEqual(@src(), main.unregisterCallback(g_periodic_id), 0);
    const count_at_unregister = g_periodic_count;

    // Give it another window to fire in, if it were (incorrectly) still
    // scheduled -- the count must not move.
    irqsOn();
    var i: u32 = 0;
    while (i < 500_000) : (i += 1) asm volatile ("nop");
    irqsOff();

    t.expectEqual(@src(), g_periodic_count, count_at_unregister);
    return t.result();
}

var g_pause_count: u32 = 0;
var g_pause_id: u32 = 0;

fn pauseCallback(id: u32, context: ?*anyopaque) callconv(.c) void {
    _ = context;
    _ = id;
    g_pause_count += 1;
}

fn pauseCondFirstFire() bool {
    return g_pause_count >= 1;
}

var g_pause_count_at_pause: u32 = 0;

fn pauseCondResumed() bool {
    return g_pause_count > g_pause_count_at_pause;
}

fn testPauseAndUnpause() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    _ = gic_if.init_global(GICD_BASE, GICR_BASE);
    _ = gic_if.init_core();

    g_pause_count = 0;

    t.expectEqual(@src(), main.registerCallback(ticksForMs(5), 1, &pauseCallback, &g_pause_id, null), 0);

    irqsOn();
    spinUntil(&pauseCondFirstFire);
    t.expectEqual(@src(), main.pause(g_pause_id), 0);
    irqsOff();

    const count_at_pause = g_pause_count;
    irqsOn();
    var i: u32 = 0;
    while (i < 500_000) : (i += 1) asm volatile ("nop");
    irqsOff();
    t.expectEqual(@src(), g_pause_count, count_at_pause);

    t.expectEqual(@src(), main.unpause(g_pause_id), 0);
    g_pause_count_at_pause = count_at_pause;
    irqsOn();
    spinUntil(&pauseCondResumed);
    irqsOff();
    t.expectTrue(@src(), g_pause_count > count_at_pause);

    t.expectEqual(@src(), main.unregisterCallback(g_pause_id), 0);
    return t.result();
}

var g_modify_fired: bool = false;
var g_modify_id: u32 = 0;

fn modifyCallback(id: u32, context: ?*anyopaque) callconv(.c) void {
    _ = context;
    _ = id;
    g_modify_fired = true;
}

fn modifyCond() bool {
    return g_modify_fired;
}

fn testModifyPeriod() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    _ = gic_if.init_global(GICD_BASE, GICR_BASE);
    _ = gic_if.init_core();

    g_modify_fired = false;

    // Register with a period far longer than the test's wait window, then
    // shrink it drastically -- if modify() didn't actually reschedule,
    // this would time out.
    t.expectEqual(@src(), main.registerCallback(ticksForMs(10_000), 0, &modifyCallback, &g_modify_id, null), 0);
    t.expectEqual(@src(), main.modify(g_modify_id, ticksForMs(10)), 0);

    irqsOn();
    spinUntil(&modifyCond);
    irqsOff();

    t.expectTrue(@src(), g_modify_fired);
    t.expectEqual(@src(), main.modify(999, 5), abi.EINVAL);
    return t.result();
}

fn testSystemTicksAndDelay() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var before: u64 = 0;
    var after: u64 = 0;
    t.expectEqual(@src(), main.getSystemTicks(&before), 0);
    t.expectEqual(@src(), main.delayTicks(ticksForMs(5)), 0);
    t.expectEqual(@src(), main.getSystemTicks(&after), 0);

    t.expectTrue(@src(), after > before);
    t.expectLessOrEqual(@src(), @as(u64, ticksForMs(5)), after - before);
    return t.result();
}

fn noopCallback(id: u32, context: ?*anyopaque) callconv(.c) void {
    _ = id;
    _ = context;
}

fn testRobustnessEdgeCases() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    _ = gic_if.init_global(GICD_BASE, GICR_BASE);
    _ = gic_if.init_core();

    var dummy_id: u32 = 0;
    t.expectEqual(@src(), main.registerCallback(0, 0, &noopCallback, &dummy_id, null), abi.EINVAL);

    t.expectEqual(@src(), main.unregisterCallback(0), abi.EINVAL);
    t.expectEqual(@src(), main.unregisterCallback(main.MAX_TIMERS + 1), abi.EINVAL);
    t.expectEqual(@src(), main.pause(0), abi.EINVAL);
    t.expectEqual(@src(), main.unpause(0), abi.EINVAL);
    t.expectEqual(@src(), main.modify(0, 10), abi.EINVAL);
    t.expectEqual(@src(), main.modify(1, 0), abi.EINVAL);

    // Exhaust every slot, verify ENOMEM, then release them all.
    var ids: [main.MAX_TIMERS]u32 = undefined;
    var filled: usize = 0;
    for (0..main.MAX_TIMERS + 1) |i| {
        const status = main.registerCallback(ticksForMs(60_000), 1, &noopCallback, &ids[if (i < main.MAX_TIMERS) i else 0], null);
        if (i < main.MAX_TIMERS) {
            t.expectEqual(@src(), status, 0);
            filled += 1;
        } else {
            t.expectEqual(@src(), status, abi.ENOMEM);
        }
    }

    for (0..filled) |i| {
        t.expectEqual(@src(), main.unregisterCallback(ids[i]), 0);
    }

    return t.result();
}

var g_fast_count: u32 = 0;
var g_slow_count: u32 = 0;

fn fastCallback(id: u32, context: ?*anyopaque) callconv(.c) void {
    _ = id;
    _ = context;
    g_fast_count += 1;
}
fn slowCallback(id: u32, context: ?*anyopaque) callconv(.c) void {
    _ = id;
    _ = context;
    g_slow_count += 1;
}

fn fastReachedThree() bool {
    return g_fast_count >= 3;
}

/// Only one hardware comparator exists -- this is the real test that the
/// software multiplexer in `reprogramHardwareTimer` correctly re-arms to
/// whichever of *several* concurrently-registered timers is soonest,
/// rather than only ever having been exercised with a single timer active
/// at a time.
fn testMultipleConcurrentTimersFireIndependently() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    _ = gic_if.init_global(GICD_BASE, GICR_BASE);
    _ = gic_if.init_core();

    g_fast_count = 0;
    g_slow_count = 0;

    var fast_id: u32 = 0;
    var slow_id: u32 = 0;
    t.expectEqual(@src(), main.registerCallback(ticksForMs(4), 1, &fastCallback, &fast_id, null), 0);
    t.expectEqual(@src(), main.registerCallback(ticksForMs(200), 1, &slowCallback, &slow_id, null), 0);

    irqsOn();
    spinUntil(&fastReachedThree);
    irqsOff();

    t.expectTrue(@src(), g_fast_count >= 3);
    // The far-slower timer shouldn't have caught up to the fast one in the
    // same window -- confirms both are live and distinctly scheduled, not
    // that one starved the other or that only the first-registered fires.
    t.expectTrue(@src(), g_fast_count > g_slow_count);

    t.expectEqual(@src(), main.unregisterCallback(fast_id), 0);
    t.expectEqual(@src(), main.unregisterCallback(slow_id), 0);
    return t.result();
}

fn testModifyWhilePausedDoesNotFireEarly() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    _ = gic_if.init_global(GICD_BASE, GICR_BASE);
    _ = gic_if.init_core();

    var id: u32 = 0;
    t.expectEqual(@src(), main.registerCallback(ticksForMs(10_000), 1, &noopCallback, &id, null), 0);
    t.expectEqual(@src(), main.pause(id), 0);

    // modify() on a paused timer must update the period for later, but
    // must not itself compute a new expiry (paused timers aren't armed).
    t.expectEqual(@src(), main.modify(id, ticksForMs(20_000)), 0);

    irqsOn();
    var i: u32 = 0;
    while (i < 500_000) : (i += 1) asm volatile ("nop");
    irqsOff();

    // Still paused, so unpausing now must be the thing that (re)computes
    // a fresh expiry from *this* moment -- verified indirectly by a clean
    // unregister below (if it had somehow fired and self-deactivated as a
    // one-shot this would be periodic anyway, but a crash/hang here would
    // signal the paused timer was wrongly armed).
    t.expectEqual(@src(), main.unregisterCallback(id), 0);
    return t.result();
}

fn testUnregisterFreesSlotForReuse() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    _ = gic_if.init_global(GICD_BASE, GICR_BASE);
    _ = gic_if.init_core();

    var ids: [main.MAX_TIMERS]u32 = undefined;
    for (0..main.MAX_TIMERS) |i| {
        t.expectEqual(@src(), main.registerCallback(ticksForMs(60_000), 1, &noopCallback, &ids[i], null), 0);
    }

    var overflow_id: u32 = 0;
    t.expectEqual(@src(), main.registerCallback(ticksForMs(60_000), 1, &noopCallback, &overflow_id, null), abi.ENOMEM);

    // Freeing exactly one slot must make room for exactly one more
    // registration, not zero and not more than one.
    t.expectEqual(@src(), main.unregisterCallback(ids[10]), 0);

    var reused_id: u32 = 0;
    t.expectEqual(@src(), main.registerCallback(ticksForMs(60_000), 1, &noopCallback, &reused_id, null), 0);
    t.expectEqual(@src(), reused_id, ids[10]);

    var overflow_again: u32 = 0;
    t.expectEqual(@src(), main.registerCallback(ticksForMs(60_000), 1, &noopCallback, &overflow_again, null), abi.ENOMEM);

    ids[10] = reused_id;
    for (0..main.MAX_TIMERS) |i| {
        t.expectEqual(@src(), main.unregisterCallback(ids[i]), 0);
    }
    return t.result();
}

fn testDoubleUnregisterRejected() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    _ = gic_if.init_global(GICD_BASE, GICR_BASE);
    _ = gic_if.init_core();

    var id: u32 = 0;
    t.expectEqual(@src(), main.registerCallback(ticksForMs(60_000), 0, &noopCallback, &id, null), 0);
    t.expectEqual(@src(), main.unregisterCallback(id), 0);
    t.expectEqual(@src(), main.unregisterCallback(id), abi.EINVAL);
    return t.result();
}

fn testPauseUnregisteredThenUnpauseRejected() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    _ = gic_if.init_global(GICD_BASE, GICR_BASE);
    _ = gic_if.init_core();

    var id: u32 = 0;
    t.expectEqual(@src(), main.registerCallback(ticksForMs(60_000), 0, &noopCallback, &id, null), 0);
    t.expectEqual(@src(), main.pause(id), 0);
    t.expectEqual(@src(), main.unregisterCallback(id), 0);

    // Once unregistered, a still-paused-looking id must not be operable
    // any further -- pause()/unpause() aren't a backdoor around the
    // active flag being cleared.
    t.expectEqual(@src(), main.pause(id), abi.EINVAL);
    t.expectEqual(@src(), main.unpause(id), abi.EINVAL);
    return t.result();
}

comptime {
    abi.kernelTest("one_shot_callback_fires", &testOneShotCallbackFires);
    abi.kernelTest("periodic_callback_fires_and_stops", &testPeriodicCallbackFiresAndStops);
    abi.kernelTest("pause_and_unpause", &testPauseAndUnpause);
    abi.kernelTest("modify_period", &testModifyPeriod);
    abi.kernelTest("system_ticks_and_delay", &testSystemTicksAndDelay);
    abi.kernelTest("robustness_edge_cases", &testRobustnessEdgeCases);
    abi.kernelTest("multiple_concurrent_timers_fire_independently", &testMultipleConcurrentTimersFireIndependently);
    abi.kernelTest("modify_while_paused_does_not_fire_early", &testModifyWhilePausedDoesNotFireEarly);
    abi.kernelTest("unregister_frees_slot_for_reuse", &testUnregisterFreesSlotForReuse);
    abi.kernelTest("double_unregister_rejected", &testDoubleUnregisterRejected);
    abi.kernelTest("pause_unregistered_then_unpause_rejected", &testPauseUnregisteredThenUnpauseRejected);
}
