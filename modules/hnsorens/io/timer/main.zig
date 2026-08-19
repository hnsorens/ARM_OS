//! Software-multiplexed timer service, exporting `Timer` (category "timer").
//! There is only one hardware comparator (the AArch64 EL1 physical
//! generic timer, `CNTP_*_EL0`), so this module keeps a table of logical
//! timer registrations and always programs the single comparator to fire
//! at the earliest pending expiry; the IRQ handler re-arms it after each
//! fire. There was no working C reference for this module (only an
//! unfinished, never-wired red-black-tree sketch in
//! `modules/hnsorens/io/timer/timer.h`) -- this is new design work guided
//! by `abi_types.Timer`'s vtable shape and the raw register access already
//! proven in `gic_v3`'s timer-interrupt test.
const std = @import("std");
const abi = @import("abi");
const kernel_test = @import("kernel_test");

pub const gic_if = abi.importInterface(abi.InterruptManager);
pub const serial_if = abi.importInterface(abi.Serial);

pub const MAX_TIMERS: usize = 64;
// QEMU virt's non-secure EL1 physical timer PPI (same vector gic_v3's own
// test exercises).
const TIMER_PPI: u32 = 30;

const TimerEntry = struct {
    active: bool = false,
    paused: bool = false,
    periodic: bool = false,
    period_ticks: u32 = 0,
    expire_at: u64 = 0,
    callback: ?abi.TimerCallback = null,
    context: ?*anyopaque = null,
};

var s_timers: [MAX_TIMERS]TimerEntry = [_]TimerEntry{.{}} ** MAX_TIMERS;
var s_lock: u64 = 0;
var s_irq_registered: bool = false;

// Same physical-register workaround as gic_v3's spinLock/spinUnlock: no
// `%w[name]` sub-register modifier in this toolchain's inline asm, so the
// exclusive-store status is bound to a hardcoded w9 with x9 clobbered.
fn spinLock(s: *volatile u64) void {
    _ = asm volatile (
        \\1: ldaxr %[ret], [%[addr]]
        \\   cbnz %[ret], 1b
        \\   stlxr w9, %[one], [%[addr]]
        \\   cbnz w9, 1b
        : [ret] "=&r" (-> u64),
        : [addr] "r" (s),
          [one] "r" (@as(u64, 1)),
        : .{ .memory = true, .x9 = true }
    );
}

fn spinUnlock(s: *volatile u64) void {
    asm volatile ("stlr %[zero], [%[addr]]"
        :
        : [zero] "r" (@as(u64, 0)),
          [addr] "r" (s),
        : .{ .memory = true }
    );
}

fn readCntpct() u64 {
    return asm volatile ("mrs %[out], cntpct_el0"
        : [out] "=r" (-> u64),
    );
}

pub fn readCntfrq() u64 {
    return asm volatile ("mrs %[out], cntfrq_el0"
        : [out] "=r" (-> u64),
    );
}

fn idxFromId(id: u32) ?usize {
    if (id == 0 or id > MAX_TIMERS) return null;
    return id - 1;
}

// Caller must hold s_lock. Programs the single hardware comparator for
// the earliest active, unpaused expiry, or disables it if there is none.
fn reprogramHardwareTimer() void {
    var earliest: ?u64 = null;
    for (s_timers) |e| {
        if (e.active and !e.paused) {
            if (earliest == null or e.expire_at < earliest.?) earliest = e.expire_at;
        }
    }
    if (earliest) |expire_at| {
        asm volatile ("msr cntp_cval_el0, %[v]"
            :
            : [v] "r" (expire_at),
        );
        asm volatile ("msr cntp_ctl_el0, %[v]"
            :
            : [v] "r" (@as(u64, 1)),
        );
    } else {
        asm volatile ("msr cntp_ctl_el0, %[v]"
            :
            : [v] "r" (@as(u64, 0)),
        );
    }
    asm volatile ("isb");
}

fn ensureInitialized() c_int {
    if (s_irq_registered) return 0;

    if (gic_if.configure(TIMER_PPI, .level, 0x80) != 0) return abi.EIO;
    if (gic_if.set_group(TIMER_PPI, .non_secure) != 0) return abi.EIO;
    if (gic_if.register_handler(TIMER_PPI, &timerIrqHandler, null) != 0) return abi.EIO;
    _ = gic_if.set_core_priority_mask(0xFF);

    s_irq_registered = true;
    return 0;
}

const DueEntry = struct {
    id: u32,
    callback: abi.TimerCallback,
    context: ?*anyopaque,
};

fn timerIrqHandler(arg: ?*anyopaque) callconv(.c) void {
    _ = arg;
    // Mask immediately so the comparator doesn't keep re-firing on the
    // stale CVAL while we recompute the next deadline.
    asm volatile ("msr cntp_ctl_el0, %[v]"
        :
        : [v] "r" (@as(u64, 0)),
    );
    asm volatile ("isb");

    var due: [MAX_TIMERS]DueEntry = undefined;
    var due_count: usize = 0;

    spinLock(&s_lock);
    const now = readCntpct();
    for (&s_timers, 0..) |*e, i| {
        if (e.active and !e.paused and e.expire_at <= now) {
            if (e.callback) |cb| {
                due[due_count] = .{ .id = @intCast(i + 1), .callback = cb, .context = e.context };
                due_count += 1;
            }
            if (e.periodic) {
                e.expire_at = now + e.period_ticks;
            } else {
                e.active = false;
            }
        }
    }
    reprogramHardwareTimer();
    spinUnlock(&s_lock);

    // Run callbacks outside the lock, same as gic_v3's dispatcher, so a
    // callback that itself calls back into this module (pause/modify/
    // register another timer) can't deadlock on s_lock.
    var i: usize = 0;
    while (i < due_count) : (i += 1) {
        due[i].callback(due[i].id, due[i].context);
    }
}

pub fn registerCallback(ticks_period: u32, periodic: u8, callback: abi.TimerCallback, out_id: *u32, context: ?*anyopaque) callconv(.c) c_int {
    if (ticks_period == 0) return abi.EINVAL;

    const init_status = ensureInitialized();
    if (init_status != 0) return init_status;

    spinLock(&s_lock);
    var slot: ?usize = null;
    for (s_timers, 0..) |e, i| {
        if (!e.active) {
            slot = i;
            break;
        }
    }
    if (slot == null) {
        spinUnlock(&s_lock);
        return abi.ENOMEM;
    }

    const idx = slot.?;
    const now = readCntpct();
    s_timers[idx] = .{
        .active = true,
        .paused = false,
        .periodic = periodic != 0,
        .period_ticks = ticks_period,
        .expire_at = now + ticks_period,
        .callback = callback,
        .context = context,
    };
    out_id.* = @intCast(idx + 1);
    reprogramHardwareTimer();
    spinUnlock(&s_lock);
    return 0;
}

pub fn unregisterCallback(id: u32) callconv(.c) c_int {
    const idx = idxFromId(id) orelse return abi.EINVAL;

    spinLock(&s_lock);
    if (!s_timers[idx].active) {
        spinUnlock(&s_lock);
        return abi.EINVAL;
    }
    s_timers[idx] = .{};
    reprogramHardwareTimer();
    spinUnlock(&s_lock);
    return 0;
}

pub fn pause(id: u32) callconv(.c) c_int {
    const idx = idxFromId(id) orelse return abi.EINVAL;

    spinLock(&s_lock);
    if (!s_timers[idx].active) {
        spinUnlock(&s_lock);
        return abi.EINVAL;
    }
    s_timers[idx].paused = true;
    reprogramHardwareTimer();
    spinUnlock(&s_lock);
    return 0;
}

pub fn unpause(id: u32) callconv(.c) c_int {
    const idx = idxFromId(id) orelse return abi.EINVAL;

    spinLock(&s_lock);
    if (!s_timers[idx].active) {
        spinUnlock(&s_lock);
        return abi.EINVAL;
    }
    if (s_timers[idx].paused) {
        s_timers[idx].paused = false;
        // Resume with a fresh full period rather than the stale
        // pre-pause deadline, so a long pause doesn't cause an
        // immediate fire the instant it's unpaused.
        s_timers[idx].expire_at = readCntpct() + s_timers[idx].period_ticks;
    }
    reprogramHardwareTimer();
    spinUnlock(&s_lock);
    return 0;
}

pub fn modify(id: u32, new_period: u32) callconv(.c) c_int {
    if (new_period == 0) return abi.EINVAL;
    const idx = idxFromId(id) orelse return abi.EINVAL;

    spinLock(&s_lock);
    if (!s_timers[idx].active) {
        spinUnlock(&s_lock);
        return abi.EINVAL;
    }
    s_timers[idx].period_ticks = new_period;
    if (!s_timers[idx].paused) {
        s_timers[idx].expire_at = readCntpct() + new_period;
    }
    reprogramHardwareTimer();
    spinUnlock(&s_lock);
    return 0;
}

pub fn getSystemTicks(ticks: *u64) callconv(.c) c_int {
    ticks.* = readCntpct();
    return 0;
}

pub fn delayTicks(ticks: u32) callconv(.c) c_int {
    const target = readCntpct() + ticks;
    while (readCntpct() < target) {
        asm volatile ("nop");
    }
    return 0;
}

comptime {
    abi.exportInterface("timer", abi.Timer, .{
        .register_callback = registerCallback,
        .unregister_callback = unregisterCallback,
        .pause = pause,
        .unpause = unpause,
        .modify = modify,
        .get_system_ticks = getSystemTicks,
        .delay_ticks = delayTicks,
    });
}

comptime {
    _ = @import("test.zig");
}
