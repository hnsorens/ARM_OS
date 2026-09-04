//! Cooperative round-robin scheduler, exporting `Scheduler` (category
//! "scheduler"). A ring buffer of ready pids; `switchToNext` rotates it
//! and hands the CPU over with `ContextSwitch.switch_to`. When the queue
//! drains, control returns to the context that called `run`.
//!
//! Single-core, cooperative, no lock: nothing reentrant touches the
//! scheduler yet (no tick preemption, no SMP). Both get added later --
//! preemption needs a "reschedule after EOIR" hook in the exception path,
//! and the lock has to be dropped across every `switch_to` (holding it
//! there would deadlock the next task's `yield`).
//!
//! Mirrors HendOS's `scheduler.c` (circular process list, `schedule_block`
//! / `schedule_unblock`) but queue-based and returning to a bootstrap
//! context rather than assuming it never stops.
const abi = @import("abi");
const kernel_fmt = @import("kernel_fmt");

pub const process_if = abi.importInterface(abi.Process);
pub const cs_if = abi.importInterface(abi.ContextSwitch);
pub const serial_if = abi.importInterface(abi.Serial);

const QCAP = 64;

var s_queue: [QCAP]u32 = [_]u32{0} ** QCAP;
var s_head: usize = 0;
var s_count: usize = 0;

var s_current: u32 = 0;
var s_bootstrap: abi.TaskContext = .{};
var s_running: bool = false;

// --- ring buffer ---------------------------------------------------

fn enq(pid: u32) bool {
    if (s_count == QCAP) return false;
    s_queue[(s_head + s_count) % QCAP] = pid;
    s_count += 1;
    return true;
}

fn deq() ?u32 {
    if (s_count == 0) return null;
    const pid = s_queue[s_head];
    s_head = (s_head + 1) % QCAP;
    s_count -= 1;
    return pid;
}

fn inQueue(pid: u32) bool {
    var i: usize = 0;
    while (i < s_count) : (i += 1) {
        if (s_queue[(s_head + i) % QCAP] == pid) return true;
    }
    return false;
}

fn removeFromQueue(pid: u32) bool {
    var i: usize = 0;
    while (i < s_count) : (i += 1) {
        if (s_queue[(s_head + i) % QCAP] != pid) continue;
        // Shift the tail down over slot i.
        var j = i;
        while (j + 1 < s_count) : (j += 1) {
            s_queue[(s_head + j) % QCAP] = s_queue[(s_head + j + 1) % QCAP];
        }
        s_count -= 1;
        return true;
    }
    return false;
}

// --- context lookup ----------------------------------------------

fn ctxPtr(pid: u32) ?*abi.TaskContext {
    var out: ?*anyopaque = null;
    if (process_if.context_of(pid, &out) != 0) return null;
    return @ptrCast(@alignCast(out orelse return null));
}

/// Save the caller's context into `from`, pick the next task (re-queuing
/// the current one first if `requeue_current`), and switch to it -- or
/// back to the bootstrap context if nothing is ready.
fn switchToNext(from: *abi.TaskContext, requeue_current: bool) void {
    const prev = s_current;
    if (requeue_current and prev != 0) {
        _ = process_if.set_state(prev, .ready);
        _ = enq(prev);
    }

    if (deq()) |next| {
        s_current = next;
        _ = process_if.set_state(next, .running);
        if (next == prev) return; // only runnable task: nothing to do
        const to = ctxPtr(next) orelse {
            s_current = 0;
            cs_if.switch_to(from, &s_bootstrap);
            return;
        };
        cs_if.switch_to(from, to);
    } else {
        s_current = 0;
        cs_if.switch_to(from, &s_bootstrap);
    }
}

// --- exported vtable --------------------------------------------

pub fn admit(pid: u32) callconv(.c) c_int {
    if (!process_if.exists(pid)) return abi.EINVAL;
    if (pid == s_current or inQueue(pid)) return abi.EEXIST;
    _ = process_if.set_state(pid, .ready);
    return if (enq(pid)) 0 else abi.ENOMEM;
}

pub fn remove(pid: u32) callconv(.c) c_int {
    if (pid == s_current) return abi.EBUSY;
    return if (removeFromQueue(pid)) 0 else abi.EINVAL;
}

pub fn current() callconv(.c) u32 {
    return s_current;
}

pub fn queueLen() callconv(.c) u32 {
    return @intCast(s_count);
}

pub fn yield() callconv(.c) void {
    const cur = s_current;
    if (cur == 0) return;
    const from = ctxPtr(cur) orelse return;
    switchToNext(from, true);
}

pub fn block() callconv(.c) c_int {
    const cur = s_current;
    if (cur == 0) return abi.EINVAL;
    const from = ctxPtr(cur) orelse return abi.EINVAL;
    _ = process_if.set_state(cur, .blocked);
    switchToNext(from, false);
    return 0;
}

pub fn stop() callconv(.c) c_int {
    const cur = s_current;
    if (cur == 0) return abi.EINVAL;
    const from = ctxPtr(cur) orelse return abi.EINVAL;
    _ = process_if.set_state(cur, .stopped);
    switchToNext(from, false);
    return 0;
}

pub fn wake(pid: u32) callconv(.c) c_int {
    var st: abi.ProcessState = .dead;
    if (process_if.get_state(pid, &st) != 0) return abi.EINVAL;
    if (st != .blocked and st != .stopped) return abi.EINVAL;
    _ = process_if.set_state(pid, .ready);
    if (inQueue(pid) or pid == s_current) return 0;
    return if (enq(pid)) 0 else abi.ENOMEM;
}

pub fn exitCurrent() callconv(.c) void {
    const cur = s_current;
    if (cur == 0) return;
    _ = process_if.set_state(cur, .zombie);
    // A throwaway context to "save" the dying task into -- never resumed.
    var scratch: abi.TaskContext = .{};
    switchToNext(&scratch, false);
    while (true) asm volatile ("wfe"); // unreachable: nothing switches back
}

pub fn run() callconv(.c) c_int {
    if (s_running) return abi.EBUSY;
    s_running = true;
    defer s_running = false;

    if (deq()) |first| {
        s_current = first;
        _ = process_if.set_state(first, .running);
        const to = ctxPtr(first) orelse {
            s_current = 0;
            return abi.EINVAL;
        };
        cs_if.switch_to(&s_bootstrap, to);
        // Resumes here once the queue drains (last task exited/blocked).
    }
    s_current = 0;
    return 0;
}

pub fn main(boot_info_ptr: *anyopaque) void {
    _ = boot_info_ptr;
    kernel_fmt.print(serial_if, "[scheduler] cooperative round-robin ready\n", .{});
}

comptime {
    abi.exportInterface("roundrobin", abi.Scheduler, .{
        .admit = admit,
        .remove = remove,
        .current = current,
        .queue_len = queueLen,
        .yield = yield,
        .block = block,
        .wake = wake,
        .stop = stop,
        .exit_current = exitCurrent,
        .run = run,
    });
}

comptime {
    _ = @import("test.zig");
}
