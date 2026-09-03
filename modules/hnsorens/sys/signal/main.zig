//! POSIX signal delivery (Layer 3), exporting `Signal` (category
//! "signal"). Per-pid disposition + pending/blocked masks; delivery on
//! the exceptions module's return-to-EL0 hook; `rt_sigreturn` undoes it.
//!
//! The rt_sigframe built on the user stack matches AArch64/Linux closely
//! enough for musl (which mostly just needs rt_sigreturn to reverse it):
//! siginfo, then ucontext { ... uc_sigmask ... uc_mcontext } with a
//! sigcontext holding x0..x30 / sp / pc / pstate and an fpsimd_context in
//! __reserved. No sigaltstack; no SA_RESTART (a signal that wakes a
//! blocked syscall just lets it return early). Fatal default actions and
//! SIGSEGV-with-no-handler end the process by invoking `exit` -- the
//! process layer owns teardown (reap/reparent/SIGCHLD).
const std = @import("std");
const abi = @import("abi");
const kernel_fmt = @import("kernel_fmt");
const spinlock = @import("spinlock");

pub const process_if = abi.importInterface(abi.Process);
pub const sched_if = abi.importInterface(abi.Scheduler);
pub const sc_if = abi.importInterface(abi.Syscalls);
pub const exc_if = abi.importInterface(abi.Exceptions);
pub const serial_if = abi.importInterface(abi.Serial);

const NSIG = 64;
const SIGILL = 4;
const SIGABRT = 6;
const SIGBUS = 7;
const SIGFPE = 8;
const SIGKILL = 9;
const SIGSEGV = 11;
const SIGCHLD = 17;
const SIGCONT = 18;
const SIGSTOP = 19;
const SIGTSTP = 20;
const SIGURG = 23;
const SIGWINCH = 28;

const SIG_DFL: u64 = 0;
const SIG_IGN: u64 = 1;
const SA_SIGINFO: u64 = 0x00000004;
const SA_NODEFER: u64 = 0x40000000;
const SA_RESETHAND: u64 = 0x80000000;

// rt_sigframe byte offsets from the frame base (== user sp on handler entry)
const SI_OFF: usize = 0;
const UC_OFF: usize = 128;
const UC_SIGMASK_OFF: usize = UC_OFF + 40;
const MC_OFF: usize = UC_OFF + 168; // struct sigcontext
const MC_FAULT_OFF: usize = MC_OFF + 0;
const MC_REGS_OFF: usize = MC_OFF + 8; // x0..x30
const MC_SP_OFF: usize = MC_REGS_OFF + 31 * 8;
const MC_PC_OFF: usize = MC_SP_OFF + 8;
const MC_PSTATE_OFF: usize = MC_PC_OFF + 8;
const MC_RESERVED_OFF: usize = MC_PSTATE_OFF + 8; // 576, 16-aligned
const FPSIMD_MAGIC: u32 = 0x46508001;
const RT_SIGFRAME_SIZE: usize = 4672;

const SigAction = struct {
    handler: u64 = 0,
    flags: u64 = 0,
    restorer: u64 = 0,
    mask: u64 = 0,
};

const SigState = struct {
    in_use: bool = false,
    pid: u32 = 0,
    blocked: u64 = 0,
    pending: u64 = 0,
    act: [NSIG + 1]SigAction = [_]SigAction{.{}} ** (NSIG + 1),
};

const MAX_TABLES = 24;
var s_sig: [MAX_TABLES]SigState = [_]SigState{.{}} ** MAX_TABLES;
var s_lock: spinlock.SpinLock = .{};

fn stateLocked(pid: u32) ?*SigState {
    for (&s_sig) |*s| {
        if (s.in_use and s.pid == pid) return s;
    }
    for (&s_sig) |*s| {
        if (!s.in_use) {
            s.* = .{ .in_use = true, .pid = pid };
            return s;
        }
    }
    return null;
}

// --- exported interface -------------------------------------------

pub fn raise(pid: u32, sig: u32) callconv(.c) void {
    if (sig == 0 or sig > NSIG) return;
    s_lock.lock();
    if (stateLocked(pid)) |s| {
        s.pending |= @as(u64, 1) << @as(u6, @intCast(sig - 1));
    }
    s_lock.unlock();
    _ = sched_if.wake(pid); // harmless if not blocked
}

pub fn forget(pid: u32) callconv(.c) void {
    s_lock.lock();
    defer s_lock.unlock();
    for (&s_sig) |*s| {
        if (s.in_use and s.pid == pid) s.* = .{};
    }
}

pub fn forkInherit(parent: u32, child: u32) callconv(.c) void {
    s_lock.lock();
    defer s_lock.unlock();
    const ps = for (&s_sig) |*s| {
        if (s.in_use and s.pid == parent) break s;
    } else return;
    const cs = stateLocked(child) orelse return;
    cs.blocked = ps.blocked;
    cs.pending = 0;
    cs.act = ps.act;
}

// --- default-action classification ------------------------------

fn defaultIsIgnore(sig: u6) bool {
    return sig == SIGCHLD or sig == SIGCONT or sig == SIGWINCH or sig == SIGURG or sig == SIGSTOP or sig == SIGTSTP;
}

// --- live FP save/restore for the sigframe ---------------------

fn saveFp(dst: [*]u8) void {
    const fpsr = asm volatile ("mrs %[v], fpsr"
        : [v] "=r" (-> u64),
    );
    const fpcr = asm volatile ("mrs %[v], fpcr"
        : [v] "=r" (-> u64),
    );
    std.mem.writeInt(u32, dst[0..4], FPSIMD_MAGIC, .little);
    std.mem.writeInt(u32, dst[4..8], 528, .little);
    std.mem.writeInt(u32, dst[8..12], @truncate(fpsr), .little);
    std.mem.writeInt(u32, dst[12..16], @truncate(fpcr), .little);
    asm volatile (
        \\ stp q0,  q1,  [%[p], #16]
        \\ stp q2,  q3,  [%[p], #48]
        \\ stp q4,  q5,  [%[p], #80]
        \\ stp q6,  q7,  [%[p], #112]
        \\ stp q8,  q9,  [%[p], #144]
        \\ stp q10, q11, [%[p], #176]
        \\ stp q12, q13, [%[p], #208]
        \\ stp q14, q15, [%[p], #240]
        \\ stp q16, q17, [%[p], #272]
        \\ stp q18, q19, [%[p], #304]
        \\ stp q20, q21, [%[p], #336]
        \\ stp q22, q23, [%[p], #368]
        \\ stp q24, q25, [%[p], #400]
        \\ stp q26, q27, [%[p], #432]
        \\ stp q28, q29, [%[p], #464]
        \\ stp q30, q31, [%[p], #496]
        :
        : [p] "r" (dst),
        : .{ .memory = true });
}

fn restoreFp(src: [*]const u8) void {
    const fpsr: u64 = std.mem.readInt(u32, src[8..12], .little);
    const fpcr: u64 = std.mem.readInt(u32, src[12..16], .little);
    asm volatile ("msr fpsr, %[v]"
        :
        : [v] "r" (fpsr),
    );
    asm volatile ("msr fpcr, %[v]"
        :
        : [v] "r" (fpcr),
    );
    asm volatile (
        \\ ldp q0,  q1,  [%[p], #16]
        \\ ldp q2,  q3,  [%[p], #48]
        \\ ldp q4,  q5,  [%[p], #80]
        \\ ldp q6,  q7,  [%[p], #112]
        \\ ldp q8,  q9,  [%[p], #144]
        \\ ldp q10, q11, [%[p], #176]
        \\ ldp q12, q13, [%[p], #208]
        \\ ldp q14, q15, [%[p], #240]
        \\ ldp q16, q17, [%[p], #272]
        \\ ldp q18, q19, [%[p], #304]
        \\ ldp q20, q21, [%[p], #336]
        \\ ldp q22, q23, [%[p], #368]
        \\ ldp q24, q25, [%[p], #400]
        \\ ldp q26, q27, [%[p], #432]
        \\ ldp q28, q29, [%[p], #464]
        \\ ldp q30, q31, [%[p], #496]
        :
        : [p] "r" (src),
        : .{ .memory = true });
}

// --- delivery (the return-to-EL0 hook) -----------------------

fn deliver(frame: *abi.TrapFrame) callconv(.c) void {
    const me = sched_if.current();
    if (me == 0) return;

    while (true) {
        s_lock.lock();
        const s = for (&s_sig) |*e| {
            if (e.in_use and e.pid == me) break e;
        } else {
            s_lock.unlock();
            return;
        };
        const ready = s.pending & ~s.blocked;
        if (ready == 0) {
            s_lock.unlock();
            return;
        }
        const sig: u6 = @intCast(@ctz(ready));
        const sig1: u32 = @as(u32, sig) + 1;
        s.pending &= ~(@as(u64, 1) << sig);
        const act = s.act[sig1];
        const old_blocked = s.blocked;

        if (act.handler == SIG_DFL) {
            if (defaultIsIgnore(@intCast(sig1))) {
                s_lock.unlock();
                continue;
            }
            s_lock.unlock();
            _ = sc_if.invoke(abi.SYS_exit, sig1, 0, 0, 0, 0, 0); // never returns
            return;
        }
        if (act.handler == SIG_IGN) {
            s_lock.unlock();
            continue;
        }

        var newly_blocked = act.mask;
        if (act.flags & SA_NODEFER == 0) newly_blocked |= @as(u64, 1) << sig;
        s.blocked |= newly_blocked;
        if (act.flags & SA_RESETHAND != 0) s.act[sig1] = .{};
        s_lock.unlock();

        var sp = frame.sp;
        sp = (sp - RT_SIGFRAME_SIZE - 16) & ~@as(u64, 0xF);
        const base: [*]u8 = @ptrFromInt(sp);

        var z: usize = 0;
        while (z < RT_SIGFRAME_SIZE) : (z += 1) base[z] = 0;

        std.mem.writeInt(i32, base[SI_OFF..][0..4], @as(i32, @intCast(sig1)), .little);
        if (sig1 == SIGSEGV or sig1 == SIGBUS or sig1 == SIGILL or sig1 == SIGFPE) {
            std.mem.writeInt(i32, base[SI_OFF + 8 ..][0..4], @as(i32, -6), .little); // SI_KERNEL
            std.mem.writeInt(u64, base[SI_OFF + 16 ..][0..8], frame.far, .little); // si_addr
        }
        std.mem.writeInt(u64, base[UC_SIGMASK_OFF..][0..8], old_blocked, .little);
        std.mem.writeInt(u64, base[MC_FAULT_OFF..][0..8], frame.far, .little);
        var r: usize = 0;
        while (r < 31) : (r += 1) {
            std.mem.writeInt(u64, base[MC_REGS_OFF + r * 8 ..][0..8], frame.x[r], .little);
        }
        std.mem.writeInt(u64, base[MC_SP_OFF..][0..8], frame.sp, .little);
        std.mem.writeInt(u64, base[MC_PC_OFF..][0..8], frame.elr, .little);
        std.mem.writeInt(u64, base[MC_PSTATE_OFF..][0..8], frame.spsr, .little);
        saveFp(base + MC_RESERVED_OFF);

        std.mem.writeInt(u64, base[RT_SIGFRAME_SIZE..][0..8], frame.x[29], .little);
        std.mem.writeInt(u64, base[RT_SIGFRAME_SIZE + 8 ..][0..8], act.restorer, .little);

        for (&frame.x) |*x| x.* = 0;
        frame.x[0] = sig1;
        frame.x[1] = if (act.flags & SA_SIGINFO != 0) sp + SI_OFF else 0;
        frame.x[2] = if (act.flags & SA_SIGINFO != 0) sp + UC_OFF else 0;
        frame.x[29] = sp + RT_SIGFRAME_SIZE;
        frame.x[30] = act.restorer;
        frame.sp = sp;
        frame.elr = act.handler;
        frame.spsr = 0; // EL0t, DAIF clear
        return; // one signal per return
    }
}

// --- syscalls -------------------------------------------------

fn sysRtSigreturn(frame: *abi.TrapFrame, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const base: [*]const u8 = @ptrFromInt(frame.sp);
    var r: usize = 0;
    while (r < 31) : (r += 1) {
        frame.x[r] = std.mem.readInt(u64, base[MC_REGS_OFF + r * 8 ..][0..8], .little);
    }
    frame.sp = std.mem.readInt(u64, base[MC_SP_OFF..][0..8], .little);
    frame.elr = std.mem.readInt(u64, base[MC_PC_OFF..][0..8], .little);
    frame.spsr = std.mem.readInt(u64, base[MC_PSTATE_OFF..][0..8], .little);
    restoreFp(base + MC_RESERVED_OFF);

    const restored = std.mem.readInt(u64, base[UC_SIGMASK_OFF..][0..8], .little);
    s_lock.lock();
    if (stateLocked(sched_if.current())) |s| s.blocked = restored;
    s_lock.unlock();
    return @bitCast(frame.x[0]);
}

fn sysRtSigaction(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const sig: usize = @intCast(a.arg[0]);
    if (sig == 0 or sig > NSIG or sig == SIGKILL or sig == SIGSTOP) return -@as(i64, abi.EINVAL);
    s_lock.lock();
    defer s_lock.unlock();
    const s = stateLocked(sched_if.current()) orelse return -@as(i64, abi.ENOMEM);
    if (a.arg[2] != 0) {
        const old: [*]u64 = @ptrFromInt(a.arg[2]);
        old[0] = s.act[sig].handler;
        old[1] = s.act[sig].flags;
        old[2] = s.act[sig].restorer;
        old[3] = s.act[sig].mask;
    }
    if (a.arg[1] != 0) {
        const new: [*]const u64 = @ptrFromInt(a.arg[1]);
        s.act[sig] = .{ .handler = new[0], .flags = new[1], .restorer = new[2], .mask = new[3] };
    }
    return 0;
}

fn sysRtSigprocmask(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const how = a.arg[0];
    s_lock.lock();
    defer s_lock.unlock();
    const s = stateLocked(sched_if.current()) orelse return -@as(i64, abi.ENOMEM);
    if (a.arg[2] != 0) {
        const old: *u64 = @ptrFromInt(a.arg[2]);
        old.* = s.blocked;
    }
    if (a.arg[1] != 0) {
        const set = @as(*const u64, @ptrFromInt(a.arg[1])).*;
        s.blocked = switch (how) {
            0 => s.blocked | set,
            1 => s.blocked & ~set,
            else => set,
        };
        s.blocked &= ~((@as(u64, 1) << (SIGKILL - 1)) | (@as(u64, 1) << (SIGSTOP - 1)));
    }
    return 0;
}

fn sysRtSigpending(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    if (a.arg[0] == 0) return 0;
    s_lock.lock();
    defer s_lock.unlock();
    const out: *u64 = @ptrFromInt(a.arg[0]);
    out.* = if (stateLocked(sched_if.current())) |s| (s.pending & s.blocked) else 0;
    return 0;
}

fn sysKill(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const target: i64 = @bitCast(a.arg[0]);
    const sig: u64 = a.arg[1];
    if (sig > NSIG) return -@as(i64, abi.EINVAL);
    if (sig == 0) return 0;
    if (target <= 0) {
        raise(sched_if.current(), @intCast(sig));
        return 0;
    }
    if (!process_if.exists(@intCast(target))) return -@as(i64, abi.ESRCH);
    raise(@intCast(target), @intCast(sig));
    return 0;
}

fn killTid(tid_arg: u64, sig: u64) i64 {
    const tid: i64 = @bitCast(tid_arg);
    if (sig > NSIG) return -@as(i64, abi.EINVAL);
    if (sig == 0) return 0;
    if (tid <= 0 or !process_if.exists(@intCast(tid))) return -@as(i64, abi.ESRCH);
    raise(@intCast(tid), @intCast(sig));
    return 0;
}

fn sysTgkill(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    return killTid(a.arg[1], a.arg[2]); // arg0 = tgid
}
fn sysTkill(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    return killTid(a.arg[0], a.arg[1]);
}

fn faultCb(frame: *abi.TrapFrame, origin: abi.ExceptionOrigin, arg: ?*anyopaque) callconv(.c) abi.ExceptionOutcome {
    _ = frame;
    _ = arg;
    if (origin != .lower_el_aarch64) return .unhandled; // real kernel fault -> halt
    const me = sched_if.current();
    if (me == 0) return .unhandled;
    raise(me, SIGSEGV);
    return .handled;
}

pub fn main(boot_info_ptr: *anyopaque) void {
    _ = boot_info_ptr;
    _ = exc_if.set_user_return_hook(&deliver);
    _ = exc_if.register_handler(.sync_data_abort, &faultCb, null);
    _ = exc_if.register_handler(.sync_instruction_abort, &faultCb, null);
    _ = sc_if.register(abi.SYS_rt_sigaction, &sysRtSigaction, null);
    _ = sc_if.register(abi.SYS_rt_sigprocmask, &sysRtSigprocmask, null);
    _ = sc_if.register(abi.SYS_rt_sigpending, &sysRtSigpending, null);
    _ = sc_if.register_raw(abi.SYS_rt_sigreturn, &sysRtSigreturn, null);
    _ = sc_if.register(abi.SYS_kill, &sysKill, null);
    _ = sc_if.register(abi.SYS_tkill, &sysTkill, null);
    _ = sc_if.register(abi.SYS_tgkill, &sysTgkill, null);
    kernel_fmt.print(serial_if, "[signal] delivery hook + rt_sig* + kill installed\n", .{});
}

comptime {
    abi.exportInterface("signal", abi.Signal, .{
        .raise = raise,
        .forget = forget,
        .fork_inherit = forkInherit,
    });
}

comptime {
    _ = @import("test.zig");
}
