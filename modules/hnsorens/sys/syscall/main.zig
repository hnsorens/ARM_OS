//! Fixed syscall dispatch table, exporting `Syscalls` (category
//! "syscalls"). Registers one `.sync_svc` callback with the exceptions
//! module; on `svc` it reads the AArch64 Linux-convention registers
//! (number in x8, args x0..x5) off the `TrapFrame`, dispatches to the
//! handler registered for that number, and writes the result back into
//! x0. `invoke` runs the same dispatch directly for kernel callers and
//! tests.
//!
//! No remap / mask indirection -- a plain array indexed by syscall
//! number, per the "simple fixed table for now" decision. HendOS's
//! `syscalls.c` was one giant `switch`; here each number's handler lives
//! in whichever module owns that facility and registers itself.
const abi = @import("abi");
const kernel_fmt = @import("kernel_fmt");
const spinlock = @import("spinlock");

pub const exc_if = abi.importInterface(abi.Exceptions);
pub const serial_if = abi.importInterface(abi.Serial);

const TABLE_SIZE = abi.SYSCALL_TABLE_SIZE;
const NR_ENOSYS: i64 = -@as(i64, abi.ENOSYS);

const Slot = struct {
    handler: ?abi.SyscallHandler = null,
    ctx: ?*anyopaque = null,
};

const RawSlot = struct {
    handler: ?abi.RawSyscallHandler = null,
    ctx: ?*anyopaque = null,
};

var s_table: [TABLE_SIZE]Slot = [_]Slot{.{}} ** TABLE_SIZE;
var s_raw: [TABLE_SIZE]RawSlot = [_]RawSlot{.{}} ** TABLE_SIZE;
var s_lock: spinlock.SpinLock = .{};

fn dispatch(nr: u64, args: *const abi.SyscallArgs) i64 {
    if (nr >= TABLE_SIZE) return NR_ENOSYS;
    s_lock.lock();
    const slot = s_table[@intCast(nr)];
    s_lock.unlock();
    if (slot.handler) |h| return h(args, slot.ctx);
    return NR_ENOSYS;
}

/// Registered with the exceptions module for `.sync_svc`. ELR already
/// points past the `svc` on entry, so nothing to advance here.
fn svcCallback(frame: *abi.TrapFrame, origin: abi.ExceptionOrigin, arg: ?*anyopaque) callconv(.c) abi.ExceptionOutcome {
    _ = origin;
    _ = arg;
    const nr = frame.x[8];

    if (nr < TABLE_SIZE) {
        s_lock.lock();
        const raw = s_raw[@intCast(nr)];
        s_lock.unlock();
        if (raw.handler) |rh| {
            const ret = rh(frame, raw.ctx);
            frame.x[0] = @bitCast(ret);
            return .handled;
        }
    }

    const args = abi.SyscallArgs{
        .nr = nr,
        .arg = .{ frame.x[0], frame.x[1], frame.x[2], frame.x[3], frame.x[4], frame.x[5] },
    };
    const ret = dispatch(nr, &args);
    frame.x[0] = @bitCast(ret);
    return .handled;
}

pub fn registerRaw(nr: u32, handler: ?abi.RawSyscallHandler, ctx: ?*anyopaque) callconv(.c) c_int {
    if (nr >= TABLE_SIZE or handler == null) return abi.EINVAL;
    s_lock.lock();
    defer s_lock.unlock();
    if (s_raw[nr].handler != null) return abi.EBUSY;
    s_raw[nr] = .{ .handler = handler, .ctx = ctx };
    return 0;
}

pub fn unregisterRaw(nr: u32) callconv(.c) c_int {
    if (nr >= TABLE_SIZE) return abi.EINVAL;
    s_lock.lock();
    defer s_lock.unlock();
    s_raw[nr] = .{};
    return 0;
}

// --- exported vtable ------------------------------------------------

pub fn register(nr: u32, handler: ?abi.SyscallHandler, ctx: ?*anyopaque) callconv(.c) c_int {
    if (nr >= TABLE_SIZE or handler == null) return abi.EINVAL;
    s_lock.lock();
    defer s_lock.unlock();
    if (s_table[nr].handler != null) return abi.EBUSY;
    s_table[nr] = .{ .handler = handler, .ctx = ctx };
    return 0;
}

pub fn unregister(nr: u32) callconv(.c) c_int {
    if (nr >= TABLE_SIZE) return abi.EINVAL;
    s_lock.lock();
    defer s_lock.unlock();
    s_table[nr] = .{};
    return 0;
}

pub fn isRegistered(nr: u32) callconv(.c) bool {
    if (nr >= TABLE_SIZE) return false;
    s_lock.lock();
    defer s_lock.unlock();
    return s_table[nr].handler != null;
}

pub fn count() callconv(.c) u32 {
    s_lock.lock();
    defer s_lock.unlock();
    var n: u32 = 0;
    for (&s_table) |*slot| {
        if (slot.handler != null) n += 1;
    }
    return n;
}

pub fn invoke(nr: u64, a0: u64, a1: u64, a2: u64, a3: u64, a4: u64, a5: u64) callconv(.c) i64 {
    const args = abi.SyscallArgs{ .nr = nr, .arg = .{ a0, a1, a2, a3, a4, a5 } };
    return dispatch(nr, &args);
}

pub fn main(boot_info_ptr: *anyopaque) void {
    _ = boot_info_ptr;
    const rc = exc_if.register_handler(.sync_svc, &svcCallback, null);
    kernel_fmt.print(serial_if, "[syscall] dispatch table ready ({d} slots), svc hook rc={d}\n", .{ TABLE_SIZE, rc });
}

comptime {
    abi.exportInterface("table", abi.Syscalls, .{
        .register = register,
        .unregister = unregister,
        .is_registered = isRegistered,
        .count = count,
        .invoke = invoke,
        .register_raw = registerRaw,
        .unregister_raw = unregisterRaw,
    });
}

comptime {
    _ = @import("test.zig");
}
