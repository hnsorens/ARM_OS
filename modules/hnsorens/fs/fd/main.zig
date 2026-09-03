//! Per-process file-descriptor tables + open-file descriptions, exporting
//! `Fd` (category "fd"). Owns the `read` / `write` / `openat` / `close` /
//! `lseek` / `dup` syscalls: each looks up `fd` in the calling process's
//! table and dispatches to that open file's backing -- the console tty,
//! or a VFS path with a byte offset (the "file pointer").
//!
//! An open-file description is shared by `dup` and across `fork` (refcount
//! + shared offset), matching POSIX. Kernel-internal callers reach the
//! per-`pid` operations directly; the syscall handlers resolve the pid
//! from `scheduler.current()`.
const abi = @import("abi");
const kernel_fmt = @import("kernel_fmt");
const spinlock = @import("spinlock");

pub const tty_if = abi.importInterface(abi.Tty);
pub const vfs_if = abi.importInterface(abi.Vfs);
pub const sched_if = abi.importInterface(abi.Scheduler);
pub const sc_if = abi.importInterface(abi.Syscalls);
pub const serial_if = abi.importInterface(abi.Serial);

const MAX_PROCESSES = 64;
const MAX_FDS = 32;
const MAX_OPEN_FILES = 128;
const PATH_MAX = 256;

const Backing = enum(u8) { none, tty, file };

const OpenFile = struct {
    backing: Backing = .none,
    refcount: u32 = 0,
    offset: u64 = 0,
    flags: u32 = 0,
    path: [PATH_MAX]u8 = [_]u8{0} ** PATH_MAX,
};

const FdTable = struct {
    in_use: bool = false,
    pid: u32 = 0,
    fds: [MAX_FDS]?*OpenFile = [_]?*OpenFile{null} ** MAX_FDS,
};

var s_open: [MAX_OPEN_FILES]OpenFile = [_]OpenFile{.{}} ** MAX_OPEN_FILES;
var s_tables: [MAX_PROCESSES]FdTable = [_]FdTable{.{}} ** MAX_PROCESSES;
var s_lock: spinlock.SpinLock = .{};

// --- pools -----------------------------------------------------

fn tableOf(pid: u32) ?*FdTable {
    for (&s_tables) |*tbl| {
        if (tbl.in_use and tbl.pid == pid) return tbl;
    }
    return null;
}

fn allocOpen() ?*OpenFile {
    for (&s_open) |*of| {
        if (of.backing == .none and of.refcount == 0) return of;
    }
    return null;
}

fn releaseOpen(of: *OpenFile) void {
    if (of.refcount > 0) of.refcount -= 1;
    if (of.refcount == 0) of.* = .{};
}

fn lowestFreeFd(tbl: *FdTable) ?u32 {
    for (tbl.fds, 0..) |slot, i| {
        if (slot == null) return @intCast(i);
    }
    return null;
}

fn pathZ(of: *OpenFile) [*:0]const u8 {
    return @ptrCast(&of.path);
}

fn copyPath(dst: *[PATH_MAX]u8, src: [*:0]const u8) void {
    var i: usize = 0;
    while (i < PATH_MAX - 1 and src[i] != 0) : (i += 1) dst[i] = src[i];
    dst[i] = 0;
}

// --- Fd vtable (process lifecycle) --------------------------

pub fn openDefaults(pid: u32) callconv(.c) c_int {
    s_lock.lock();
    defer s_lock.unlock();

    if (tableOf(pid) != null) return abi.EEXIST;
    const tbl = for (&s_tables) |*t| {
        if (!t.in_use) break t;
    } else return abi.ENOMEM;

    const con = allocOpen() orelse return abi.ENFILE;
    con.* = .{ .backing = .tty, .refcount = 3, .offset = 0 };

    tbl.* = .{ .in_use = true, .pid = pid };
    tbl.fds[0] = con;
    tbl.fds[1] = con;
    tbl.fds[2] = con;
    return 0;
}

pub fn forkTable(parent: u32, child: u32) callconv(.c) c_int {
    s_lock.lock();
    defer s_lock.unlock();

    const src = tableOf(parent) orelse return abi.EINVAL;
    if (tableOf(child) != null) return abi.EEXIST;
    const dst = for (&s_tables) |*t| {
        if (!t.in_use) break t;
    } else return abi.ENOMEM;

    dst.* = .{ .in_use = true, .pid = child };
    for (src.fds, 0..) |slot, i| {
        if (slot) |of| {
            of.refcount += 1;
            dst.fds[i] = of;
        }
    }
    return 0;
}

pub fn clearTable(pid: u32) callconv(.c) c_int {
    s_lock.lock();
    defer s_lock.unlock();

    const tbl = tableOf(pid) orelse return abi.EINVAL;
    for (&tbl.fds) |*slot| {
        if (slot.*) |of| releaseOpen(of);
        slot.* = null;
    }
    tbl.* = .{};
    return 0;
}

// --- per-pid file ops (syscall bodies) --------------------

pub fn fdRead(pid: u32, fd: u32, buf: [*]u8, count: u64) i64 {
    s_lock.lock();
    const tbl = tableOf(pid) orelse {
        s_lock.unlock();
        return -@as(i64, abi.EBADF);
    };
    if (fd >= MAX_FDS or tbl.fds[fd] == null) {
        s_lock.unlock();
        return -@as(i64, abi.EBADF);
    }
    const of = tbl.fds[fd].?;
    s_lock.unlock();

    switch (of.backing) {
        .tty => return @intCast(tty_if.read(buf, count)),
        .file => {
            var got: u64 = 0;
            const rc = vfs_if.read(pathZ(of), of.offset, buf, count, &got);
            if (rc != 0) return -@as(i64, rc);
            of.offset += got;
            return @intCast(got);
        },
        .none => return -@as(i64, abi.EBADF),
    }
}

pub fn fdWrite(pid: u32, fd: u32, buf: [*]const u8, count: u64) i64 {
    s_lock.lock();
    const tbl = tableOf(pid) orelse {
        s_lock.unlock();
        return -@as(i64, abi.EBADF);
    };
    if (fd >= MAX_FDS or tbl.fds[fd] == null) {
        s_lock.unlock();
        return -@as(i64, abi.EBADF);
    }
    const of = tbl.fds[fd].?;
    s_lock.unlock();

    switch (of.backing) {
        .tty => return @intCast(tty_if.write(buf, count)),
        .file => {
            var put: u64 = 0;
            const rc = vfs_if.write(pathZ(of), of.offset, buf, count, &put);
            if (rc != 0) return -@as(i64, rc);
            of.offset += put;
            return @intCast(put);
        },
        .none => return -@as(i64, abi.EBADF),
    }
}

pub fn fdOpenat(pid: u32, path: [*:0]const u8, flags: u32, mode: u16) i64 {
    // Only absolute paths for now (no per-process cwd yet); a relative
    // path is taken as relative to the root.
    var zbuf: [PATH_MAX]u8 = [_]u8{0} ** PATH_MAX;
    if (path[0] == '/') {
        copyPath(&zbuf, path);
    } else {
        zbuf[0] = '/';
        var i: usize = 0;
        while (i < PATH_MAX - 2 and path[i] != 0) : (i += 1) zbuf[i + 1] = path[i];
    }
    const zp: [*:0]const u8 = @ptrCast(&zbuf);

    var ino: u32 = 0;
    var ft: u8 = 0;
    var rc = vfs_if.resolve(zp, &ino, &ft);
    if (rc == abi.ENOENT and (flags & abi.O_CREAT) != 0) {
        rc = vfs_if.create(zp, mode);
    }
    if (rc != 0) return -@as(i64, rc);

    s_lock.lock();
    defer s_lock.unlock();

    const tbl = tableOf(pid) orelse return -@as(i64, abi.EBADF);
    const slot = lowestFreeFd(tbl) orelse return -@as(i64, abi.EMFILE);
    const of = allocOpen() orelse return -@as(i64, abi.ENFILE);

    var start: u64 = 0;
    if ((flags & abi.O_APPEND) != 0) {
        var st: abi.Ext2Stat = .{};
        if (vfs_if.stat(zp, &st) == 0) start = st.size;
    }

    of.* = .{ .backing = .file, .refcount = 1, .offset = start, .flags = flags };
    copyPath(&of.path, zp);
    tbl.fds[slot] = of;
    return @intCast(slot);
}

pub fn fdClose(pid: u32, fd: u32) i64 {
    s_lock.lock();
    defer s_lock.unlock();
    const tbl = tableOf(pid) orelse return -@as(i64, abi.EBADF);
    if (fd >= MAX_FDS or tbl.fds[fd] == null) return -@as(i64, abi.EBADF);
    releaseOpen(tbl.fds[fd].?);
    tbl.fds[fd] = null;
    return 0;
}

pub fn fdLseek(pid: u32, fd: u32, offset: i64, whence: u32) i64 {
    s_lock.lock();
    const tbl = tableOf(pid) orelse {
        s_lock.unlock();
        return -@as(i64, abi.EBADF);
    };
    if (fd >= MAX_FDS or tbl.fds[fd] == null) {
        s_lock.unlock();
        return -@as(i64, abi.EBADF);
    }
    const of = tbl.fds[fd].?;
    s_lock.unlock();

    if (of.backing != .file) return -@as(i64, abi.ESPIPE);

    var base: i64 = 0;
    switch (whence) {
        abi.SEEK_SET => base = 0,
        abi.SEEK_CUR => base = @intCast(of.offset),
        abi.SEEK_END => {
            var st: abi.Ext2Stat = .{};
            if (vfs_if.stat(pathZ(of), &st) != 0) return -@as(i64, abi.EIO);
            base = @intCast(st.size);
        },
        else => return -@as(i64, abi.EINVAL),
    }
    const target = base + offset;
    if (target < 0) return -@as(i64, abi.EINVAL);
    of.offset = @intCast(target);
    return target;
}

pub fn fdDup(pid: u32, fd: u32) i64 {
    s_lock.lock();
    defer s_lock.unlock();
    const tbl = tableOf(pid) orelse return -@as(i64, abi.EBADF);
    if (fd >= MAX_FDS or tbl.fds[fd] == null) return -@as(i64, abi.EBADF);
    const slot = lowestFreeFd(tbl) orelse return -@as(i64, abi.EMFILE);
    const of = tbl.fds[fd].?;
    of.refcount += 1;
    tbl.fds[slot] = of;
    return @intCast(slot);
}

// --- syscall handlers ------------------------------------

fn sysRead(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    if (args.arg[2] == 0) return 0;
    return fdRead(sched_if.current(), @intCast(args.arg[0]), @ptrFromInt(args.arg[1]), args.arg[2]);
}

fn sysWrite(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    if (args.arg[2] == 0) return 0;
    return fdWrite(sched_if.current(), @intCast(args.arg[0]), @ptrFromInt(args.arg[1]), args.arg[2]);
}

fn sysOpenat(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    return fdOpenat(sched_if.current(), @ptrFromInt(args.arg[1]), @truncate(args.arg[2]), @truncate(args.arg[3]));
}

fn sysClose(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    return fdClose(sched_if.current(), @intCast(args.arg[0]));
}

fn sysLseek(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    return fdLseek(sched_if.current(), @intCast(args.arg[0]), @bitCast(args.arg[1]), @truncate(args.arg[2]));
}

fn sysDup(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    return fdDup(sched_if.current(), @intCast(args.arg[0]));
}

pub fn main(boot_info_ptr: *anyopaque) void {
    _ = boot_info_ptr;
    _ = sc_if.register(abi.SYS_read, &sysRead, null);
    _ = sc_if.register(abi.SYS_write, &sysWrite, null);
    _ = sc_if.register(abi.SYS_openat, &sysOpenat, null);
    _ = sc_if.register(abi.SYS_close, &sysClose, null);
    _ = sc_if.register(abi.SYS_lseek, &sysLseek, null);
    _ = sc_if.register(abi.SYS_dup, &sysDup, null);
    kernel_fmt.print(serial_if, "[fd] descriptor tables + read/write/openat/close/lseek/dup\n", .{});
}

// --- test hooks --------------------------------------------

pub fn testOpenFileCount() u32 {
    var n: u32 = 0;
    for (s_open) |of| {
        if (of.backing != .none) n += 1;
    }
    return n;
}

/// 0 = no fd, 1 = tty, 2 = file (mirrors the Backing enum).
pub fn testFdBacking(pid: u32, fd: u32) u8 {
    const tbl = tableOf(pid) orelse return 0;
    if (fd >= MAX_FDS or tbl.fds[fd] == null) return 0;
    return @intFromEnum(tbl.fds[fd].?.backing);
}

comptime {
    abi.exportInterface("fds", abi.Fd, .{
        .open_defaults = openDefaults,
        .fork_table = forkTable,
        .clear_table = clearTable,
    });
}

comptime {
    _ = @import("test.zig");
}
