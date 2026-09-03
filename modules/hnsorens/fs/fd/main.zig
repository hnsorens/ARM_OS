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
const std = @import("std");
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
const MAX_PIPES = 32;
const PIPE_BUF = 4096;
const PATH_MAX = 256;

const O_NONBLOCK: u32 = 0o4000;

// backing: none | tty | file (VFS path + offset) | dev (special char dev,
// path is /dev/null etc.) | pipe (offset = pipe index, flags bit0 = write end)
const Backing = enum(u8) { none, tty, file, dev, pipe };

const OpenFile = struct {
    backing: Backing = .none,
    refcount: u32 = 0,
    offset: u64 = 0,
    flags: u32 = 0,
    path: [PATH_MAX]u8 = [_]u8{0} ** PATH_MAX,
};

const Pipe = struct {
    in_use: bool = false,
    buf: [PIPE_BUF]u8 = undefined,
    head: usize = 0,
    count: usize = 0,
    readers: u32 = 0,
    writers: u32 = 0,
    blocked_reader: u32 = 0,
    blocked_writer: u32 = 0,
};

const FdTable = struct {
    in_use: bool = false,
    pid: u32 = 0,
    cwd: [PATH_MAX]u8 = [_]u8{0} ** PATH_MAX, // "" == "/"
    fds: [MAX_FDS]?*OpenFile = [_]?*OpenFile{null} ** MAX_FDS,
};

var s_open: [MAX_OPEN_FILES]OpenFile = [_]OpenFile{.{}} ** MAX_OPEN_FILES;
var s_tables: [MAX_PROCESSES]FdTable = [_]FdTable{.{}} ** MAX_PROCESSES;
var s_pipes: [MAX_PIPES]Pipe = [_]Pipe{.{}} ** MAX_PIPES;
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

// caller holds s_lock
fn releaseOpen(of: *OpenFile) void {
    if (of.refcount > 0) of.refcount -= 1;
    if (of.refcount != 0) return;
    if (of.backing == .pipe) {
        const p = &s_pipes[@intCast(of.offset)];
        if (of.flags & 1 != 0) {
            if (p.writers > 0) p.writers -= 1;
            if (p.writers == 0 and p.blocked_reader != 0) {
                const r = p.blocked_reader;
                p.blocked_reader = 0;
                _ = sched_if.wake(r);
            }
        } else {
            if (p.readers > 0) p.readers -= 1;
            if (p.readers == 0 and p.blocked_writer != 0) {
                const w = p.blocked_writer;
                p.blocked_writer = 0;
                _ = sched_if.wake(w);
            }
        }
        if (p.readers == 0 and p.writers == 0) p.* = .{};
    }
    of.* = .{};
}

fn allocPipe() ?usize {
    for (&s_pipes, 0..) |*p, i| {
        if (!p.in_use) {
            p.* = .{ .in_use = true, .readers = 1, .writers = 1 };
            return i;
        }
    }
    return null;
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
    tbl.cwd[0] = '/';
    tbl.cwd[1] = 0;
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
    @memcpy(&dst.cwd, &src.cwd);
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

// --- path resolution (cwd-aware; the VFS handles `.`/`..` components) --

var s_rng: u64 = 0x243F6A8885A308D3;
fn rnd() u8 {
    var x = s_rng;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    s_rng = x;
    return @truncate(x >> 24);
}

fn appendZ(dst: *[PATH_MAX]u8, at: *usize, s: [*:0]const u8) void {
    var i: usize = 0;
    while (s[i] != 0 and at.* < PATH_MAX - 1) : (i += 1) {
        dst[at.*] = s[i];
        at.* += 1;
    }
    dst[at.*] = 0;
}

/// Resolves `path` against pid's cwd (for a relative path) into `out`.
fn resolvePath(pid: u32, path: [*:0]const u8, out: *[PATH_MAX]u8) void {
    if (path[0] == '/') {
        copyPath(out, path);
        return;
    }
    var n: usize = 0;
    s_lock.lock();
    if (tableOf(pid)) |tbl| {
        var i: usize = 0;
        while (i < PATH_MAX - 1 and tbl.cwd[i] != 0) : (i += 1) {
            out[i] = tbl.cwd[i];
        }
        n = i;
    }
    s_lock.unlock();
    if (n == 0) {
        out[0] = '/';
        n = 1;
    }
    if (out[n - 1] != '/' and n < PATH_MAX - 1) {
        out[n] = '/';
        n += 1;
    }
    out[n] = 0;
    appendZ(out, &n, path);
}

// --- special /dev nodes -------------------------------------
// of.offset for a .dev backing: 0=null 1=zero 2=full 3=urandom

fn devKind(p: [*:0]const u8) ?u64 {
    const s = std.mem.span(p);
    if (std.mem.eql(u8, s, "/dev/null")) return 0;
    if (std.mem.eql(u8, s, "/dev/zero")) return 1;
    if (std.mem.eql(u8, s, "/dev/full")) return 2;
    if (std.mem.eql(u8, s, "/dev/urandom") or std.mem.eql(u8, s, "/dev/random")) return 3;
    return null;
}

// --- per-pid file ops (syscall bodies) --------------------

fn pipeRead(p: *Pipe, pid: u32, buf: [*]u8, count: u64, nonblock: bool) i64 {
    while (true) {
        s_lock.lock();
        if (p.count > 0) {
            var i: u64 = 0;
            while (i < count and p.count > 0) : (i += 1) {
                buf[i] = p.buf[p.head];
                p.head = (p.head + 1) % PIPE_BUF;
                p.count -= 1;
            }
            if (p.blocked_writer != 0) {
                const w = p.blocked_writer;
                p.blocked_writer = 0;
                _ = sched_if.wake(w);
            }
            s_lock.unlock();
            return @intCast(i);
        }
        if (p.writers == 0) {
            s_lock.unlock();
            return 0; // EOF
        }
        if (nonblock or sched_if.current() == 0) {
            s_lock.unlock();
            return -@as(i64, abi.EAGAIN);
        }
        p.blocked_reader = pid;
        s_lock.unlock();
        _ = sched_if.block();
    }
}

fn pipeWrite(p: *Pipe, pid: u32, buf: [*]const u8, count: u64, nonblock: bool) i64 {
    var done: u64 = 0;
    while (done < count) {
        s_lock.lock();
        if (p.readers == 0) {
            s_lock.unlock();
            return if (done > 0) @intCast(done) else -@as(i64, abi.EPIPE);
        }
        var wrote_any = false;
        while (done < count and p.count < PIPE_BUF) : (done += 1) {
            p.buf[(p.head + p.count) % PIPE_BUF] = buf[done];
            p.count += 1;
            wrote_any = true;
        }
        if (wrote_any and p.blocked_reader != 0) {
            const r = p.blocked_reader;
            p.blocked_reader = 0;
            _ = sched_if.wake(r);
        }
        if (done >= count) {
            s_lock.unlock();
            return @intCast(done);
        }
        // buffer full
        if (nonblock or sched_if.current() == 0) {
            s_lock.unlock();
            return if (done > 0) @intCast(done) else -@as(i64, abi.EAGAIN);
        }
        p.blocked_writer = pid;
        s_lock.unlock();
        _ = sched_if.block();
    }
    return @intCast(done);
}

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
        .pipe => return pipeRead(&s_pipes[@intCast(of.offset)], pid, buf, count, of.flags & O_NONBLOCK != 0),
        .dev => switch (of.offset) {
            0 => return 0, // /dev/null EOF
            1 => { // /dev/zero
                var i: u64 = 0;
                while (i < count) : (i += 1) buf[i] = 0;
                return @intCast(count);
            },
            2 => { // /dev/full: reads as zero
                var i: u64 = 0;
                while (i < count) : (i += 1) buf[i] = 0;
                return @intCast(count);
            },
            else => { // /dev/urandom
                var i: u64 = 0;
                while (i < count) : (i += 1) buf[i] = rnd();
                return @intCast(count);
            },
        },
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
        .pipe => return pipeWrite(&s_pipes[@intCast(of.offset)], pid, buf, count, of.flags & O_NONBLOCK != 0),
        .dev => switch (of.offset) {
            2 => return -@as(i64, abi.ENOSPC), // /dev/full
            else => return @intCast(count), // null / zero / urandom: discard
        },
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
    var zbuf: [PATH_MAX]u8 = [_]u8{0} ** PATH_MAX;
    resolvePath(pid, path, &zbuf);
    const zp: [*:0]const u8 = @ptrCast(&zbuf);

    // Special device nodes (no devfs yet -- recognised by path).
    const dk = devKind(zp);
    const is_devtty = blk: {
        const s = std.mem.span(zp);
        break :blk std.mem.eql(u8, s, "/dev/tty") or std.mem.eql(u8, s, "/dev/console") or std.mem.eql(u8, s, "/dev/stdin") or std.mem.eql(u8, s, "/dev/stdout") or std.mem.eql(u8, s, "/dev/stderr");
    };

    if (dk == null and !is_devtty) {
        var ino: u32 = 0;
        var ft: u8 = 0;
        var rc = vfs_if.resolve(zp, &ino, &ft);
        if (rc == abi.ENOENT and (flags & abi.O_CREAT) != 0) {
            rc = vfs_if.create(zp, mode);
        }
        if (rc != 0) return -@as(i64, rc);
        if (rc == 0 and ft == abi.EXT2_FT_REG_FILE and (flags & abi.O_TRUNC) != 0 and (flags & (abi.O_WRONLY | abi.O_RDWR)) != 0) {
            _ = vfs_if.truncate(zp, 0);
        }
    }

    s_lock.lock();
    defer s_lock.unlock();

    const tbl = tableOf(pid) orelse return -@as(i64, abi.EBADF);
    const slot = lowestFreeFd(tbl) orelse return -@as(i64, abi.EMFILE);
    const of = allocOpen() orelse return -@as(i64, abi.ENFILE);

    if (is_devtty) {
        of.* = .{ .backing = .tty, .refcount = 1, .flags = flags };
    } else if (dk) |kind| {
        of.* = .{ .backing = .dev, .refcount = 1, .offset = kind, .flags = flags };
    } else {
        var start: u64 = 0;
        if ((flags & abi.O_APPEND) != 0) {
            var st: abi.Ext2Stat = .{};
            if (vfs_if.stat(zp, &st) == 0) start = st.size;
        }
        of.* = .{ .backing = .file, .refcount = 1, .offset = start, .flags = flags };
        copyPath(&of.path, zp);
    }
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

// --- path resolution for the *at() family -------------------------
//
// dirfd is assumed AT_FDCWD: an absolute path is used verbatim, a
// relative one is joined onto the caller's cwd (VFS handles `.`/`..`).

fn resolveAt(path: [*:0]const u8, out: *[PATH_MAX]u8) void {
    resolvePath(sched_if.current(), path, out);
}

fn dtypeFor(ext2_ft: u8) u8 {
    return switch (ext2_ft) {
        abi.EXT2_FT_REG_FILE => abi.DT_REG,
        abi.EXT2_FT_DIR => abi.DT_DIR,
        abi.EXT2_FT_CHRDEV => abi.DT_CHR,
        abi.EXT2_FT_BLKDEV => abi.DT_BLK,
        abi.EXT2_FT_FIFO => abi.DT_FIFO,
        abi.EXT2_FT_SOCK => abi.DT_SOCK,
        abi.EXT2_FT_SYMLINK => abi.DT_LNK,
        else => abi.DT_UNKNOWN,
    };
}

fn fillKStat(zp: [*:0]const u8, follow: bool, out: *abi.KStat) c_int {
    var est: abi.Ext2Stat = .{};
    const rc = if (follow) vfs_if.stat(zp, &est) else vfs_if.lstat(zp, &est);
    if (rc != 0) return rc;
    out.* = .{
        .st_dev = 1,
        .st_ino = est.inode_num,
        .st_mode = est.mode, // ext2 mode already carries the S_IF* type bits
        .st_nlink = est.links_count,
        .st_rdev = est.rdev,
        .st_size = @intCast(est.size),
        .st_blksize = 4096,
        .st_blocks = @intCast((est.size + 511) / 512),
        .st_atime = est.atime,
        .st_mtime = est.mtime,
        .st_ctime = est.ctime,
    };
    return 0;
}

fn ttyKStat(out: *abi.KStat) void {
    out.* = .{
        .st_dev = 1,
        .st_ino = 1,
        .st_mode = 0x2000 | 0o620, // S_IFCHR | rw--w----
        .st_nlink = 1,
        .st_rdev = (5 << 8) | 1, // /dev/console-ish
        .st_blksize = 1024,
    };
}

// --- iovec (writev/readv) ---------------------------------------

const IoVec = extern struct { base: u64, len: u64 };

fn sysWritev(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const pid = sched_if.current();
    const fd: u32 = @intCast(args.arg[0]);
    const iov: [*]const IoVec = @ptrFromInt(args.arg[1]);
    const n: usize = @intCast(args.arg[2]);
    var total: i64 = 0;
    var i: usize = 0;
    while (i < n) : (i += 1) {
        if (iov[i].len == 0) continue;
        const r = fdWrite(pid, fd, @ptrFromInt(iov[i].base), iov[i].len);
        if (r < 0) return if (total > 0) total else r;
        total += r;
        if (@as(u64, @intCast(r)) < iov[i].len) break; // short write
    }
    return total;
}

fn sysReadv(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const pid = sched_if.current();
    const fd: u32 = @intCast(args.arg[0]);
    const iov: [*]const IoVec = @ptrFromInt(args.arg[1]);
    const n: usize = @intCast(args.arg[2]);
    var total: i64 = 0;
    var i: usize = 0;
    while (i < n) : (i += 1) {
        if (iov[i].len == 0) continue;
        const r = fdRead(pid, fd, @ptrFromInt(iov[i].base), iov[i].len);
        if (r < 0) return if (total > 0) total else r;
        total += r;
        if (@as(u64, @intCast(r)) < iov[i].len) break; // short read / EOF
    }
    return total;
}

// --- stat family ----------------------------------------------

fn sysFstat(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const pid = sched_if.current();
    const fd: u32 = @intCast(args.arg[0]);
    const out: *abi.KStat = @ptrFromInt(args.arg[1]);

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
        .tty, .dev => {
            ttyKStat(out);
            if (of.backing == .dev) out.st_mode = 0x2000 | 0o666; // S_IFCHR
            return 0;
        },
        .pipe => {
            out.* = .{ .st_dev = 1, .st_ino = 1, .st_mode = 0x1000 | 0o600, .st_nlink = 1, .st_blksize = PIPE_BUF }; // S_IFIFO
            return 0;
        },
        .file => {
            const rc = fillKStat(pathZ(of), true, out);
            return if (rc != 0) -@as(i64, rc) else 0;
        },
        .none => return -@as(i64, abi.EBADF),
    }
}

fn sysNewfstatat(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const path: [*:0]const u8 = @ptrFromInt(args.arg[1]);
    const out: *abi.KStat = @ptrFromInt(args.arg[2]);
    const flags = args.arg[3];

    // AT_EMPTY_PATH with dirfd -> fstat that fd.
    if ((flags & abi.AT_EMPTY_PATH) != 0 and path[0] == 0) {
        var a2 = args.*;
        a2.arg[1] = args.arg[2];
        return sysFstat(&a2, null);
    }
    var zbuf: [PATH_MAX]u8 = undefined;
    resolveAt(path, &zbuf);
    const rc = fillKStat(@ptrCast(&zbuf), (flags & abi.AT_SYMLINK_NOFOLLOW) == 0, out);
    return if (rc != 0) -@as(i64, rc) else 0;
}

// --- getdents64 ---------------------------------------------

fn sysGetdents64(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const pid = sched_if.current();
    const fd: u32 = @intCast(args.arg[0]);
    const ubuf: [*]u8 = @ptrFromInt(args.arg[1]);
    const cap: usize = @intCast(args.arg[2]);

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
    if (of.backing != .file) return -@as(i64, abi.ENOTDIR);

    var written: usize = 0;
    var idx: u32 = @intCast(of.offset);
    while (true) {
        var e: abi.Ext2DirEntry = .{};
        if (vfs_if.list_dir(pathZ(of), idx, &e) != 0) break; // end of directory
        const namelen: usize = e.name_len;
        const reclen = (19 + namelen + 1 + 7) & ~@as(usize, 7);
        if (written + reclen > cap) {
            if (written == 0) return -@as(i64, abi.EINVAL); // buffer too small for one entry
            break;
        }
        const rec = ubuf + written;
        std.mem.writeInt(u64, rec[0..8], e.inode, .little);
        std.mem.writeInt(u64, rec[8..16], idx + 1, .little); // d_off
        std.mem.writeInt(u16, rec[16..18], @intCast(reclen), .little);
        rec[18] = dtypeFor(e.file_type);
        var k: usize = 0;
        while (k < namelen) : (k += 1) rec[19 + k] = e.name[k];
        rec[19 + namelen] = 0;
        var pad = 19 + namelen + 1;
        while (pad < reclen) : (pad += 1) rec[pad] = 0;
        written += reclen;
        idx += 1;
    }
    of.offset = idx;
    return @intCast(written);
}

// --- fcntl / dup3 -----------------------------------------

fn sysFcntl(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const pid = sched_if.current();
    const fd: u32 = @intCast(args.arg[0]);
    const cmd = args.arg[1];

    s_lock.lock();
    defer s_lock.unlock();
    const tbl = tableOf(pid) orelse return -@as(i64, abi.EBADF);
    if (fd >= MAX_FDS or tbl.fds[fd] == null) return -@as(i64, abi.EBADF);
    const of = tbl.fds[fd].?;

    switch (cmd) {
        abi.F_DUPFD, abi.F_DUPFD_CLOEXEC => {
            const min: u32 = @intCast(args.arg[2]);
            var i: u32 = min;
            while (i < MAX_FDS) : (i += 1) {
                if (tbl.fds[i] == null) {
                    of.refcount += 1;
                    tbl.fds[i] = of;
                    return @intCast(i);
                }
            }
            return -@as(i64, abi.EMFILE);
        },
        abi.F_GETFD => return 0, // FD_CLOEXEC not tracked yet
        abi.F_SETFD => return 0,
        abi.F_GETFL => return @intCast(of.flags),
        abi.F_SETFL => {
            of.flags = @truncate(args.arg[2]);
            return 0;
        },
        else => return -@as(i64, abi.EINVAL),
    }
}

fn sysDup3(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const pid = sched_if.current();
    const oldfd: u32 = @intCast(args.arg[0]);
    const newfd: u32 = @intCast(args.arg[1]);

    s_lock.lock();
    defer s_lock.unlock();
    const tbl = tableOf(pid) orelse return -@as(i64, abi.EBADF);
    if (oldfd >= MAX_FDS or tbl.fds[oldfd] == null or newfd >= MAX_FDS) return -@as(i64, abi.EBADF);
    if (oldfd == newfd) return -@as(i64, abi.EINVAL); // dup3 (unlike dup2) errors here
    if (tbl.fds[newfd]) |old| releaseOpen(old);
    const of = tbl.fds[oldfd].?;
    of.refcount += 1;
    tbl.fds[newfd] = of;
    return @intCast(newfd);
}

// --- access / ioctl -------------------------------------

fn faccess(path: [*:0]const u8) i64 {
    var zbuf: [PATH_MAX]u8 = undefined;
    resolveAt(path, &zbuf);
    var ino: u32 = 0;
    var ft: u8 = 0;
    const rc = vfs_if.resolve(@ptrCast(&zbuf), &ino, &ft);
    return if (rc != 0) -@as(i64, rc) else 0;
}

fn sysFaccessat(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    return faccess(@ptrFromInt(args.arg[1]));
}
fn sysFaccessat2(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    return faccess(@ptrFromInt(args.arg[1]));
}

fn sysIoctl(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const pid = sched_if.current();
    const fd: u32 = @intCast(args.arg[0]);
    const req = args.arg[1];
    const argp = args.arg[2];

    s_lock.lock();
    const tbl = tableOf(pid) orelse {
        s_lock.unlock();
        return -@as(i64, abi.EBADF);
    };
    const is_tty = fd < MAX_FDS and tbl.fds[fd] != null and tbl.fds[fd].?.backing == .tty;
    s_lock.unlock();

    if (!is_tty) return -@as(i64, abi.ENOTTY);

    switch (req) {
        abi.TCGETS => {
            // struct termios: c_iflag,c_oflag,c_cflag,c_lflag (4x u32),
            // c_line (u8), c_cc[19], then speeds. Zero it; enough for
            // musl's isatty (it only checks the syscall succeeds).
            if (argp != 0) {
                const p: [*]u8 = @ptrFromInt(argp);
                var i: usize = 0;
                while (i < 60) : (i += 1) p[i] = 0;
                std.mem.writeInt(u32, p[0..4], 0o000005, .little); // c_iflag ICRNL|BRKINT-ish
                std.mem.writeInt(u32, p[4..8], 0o000005, .little); // c_oflag OPOST|ONLCR-ish
                std.mem.writeInt(u32, p[8..12], 0o000277, .little); // c_cflag
                std.mem.writeInt(u32, p[12..16], 0o105073, .little); // c_lflag ICANON|ECHO|...
            }
            return 0;
        },
        abi.TCSETS, abi.TCSETSW, abi.TCSETSF => return 0, // accept, ignore
        abi.TIOCGWINSZ => {
            if (argp != 0) {
                const p: [*]u8 = @ptrFromInt(argp);
                std.mem.writeInt(u16, p[0..2], 24, .little); // ws_row
                std.mem.writeInt(u16, p[2..4], 80, .little); // ws_col
                std.mem.writeInt(u16, p[4..6], 0, .little);
                std.mem.writeInt(u16, p[6..8], 0, .little);
            }
            return 0;
        },
        abi.TIOCSWINSZ, abi.TIOCSPGRP => return 0,
        abi.TIOCGPGRP => {
            if (argp != 0) std.mem.writeInt(u32, @as([*]u8, @ptrFromInt(argp))[0..4], sched_if.current(), .little);
            return 0;
        },
        else => return -@as(i64, abi.ENOTTY),
    }
}

// --- filesystem mutators (thin vfs wrappers) -----------------

fn sysMkdirat(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    var zbuf: [PATH_MAX]u8 = undefined;
    resolveAt(@ptrFromInt(args.arg[1]), &zbuf);
    const rc = vfs_if.mkdir(@ptrCast(&zbuf), @truncate(args.arg[2]));
    return if (rc != 0) -@as(i64, rc) else 0;
}

fn sysUnlinkat(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    var zbuf: [PATH_MAX]u8 = undefined;
    resolveAt(@ptrFromInt(args.arg[1]), &zbuf);
    const rc = vfs_if.remove(@ptrCast(&zbuf)); // vfs.remove handles dir vs file
    return if (rc != 0) -@as(i64, rc) else 0;
}

fn sysRenameat(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    var ob: [PATH_MAX]u8 = undefined;
    var nb: [PATH_MAX]u8 = undefined;
    resolveAt(@ptrFromInt(args.arg[1]), &ob);
    resolveAt(@ptrFromInt(args.arg[3]), &nb);
    const rc = vfs_if.rename(@ptrCast(&ob), @ptrCast(&nb));
    return if (rc != 0) -@as(i64, rc) else 0;
}

fn sysSymlinkat(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const target: [*:0]const u8 = @ptrFromInt(args.arg[0]);
    var lb: [PATH_MAX]u8 = undefined;
    resolveAt(@ptrFromInt(args.arg[2]), &lb);
    const rc = vfs_if.symlink(target, @ptrCast(&lb));
    return if (rc != 0) -@as(i64, rc) else 0;
}

fn sysReadlinkat(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    var zbuf: [PATH_MAX]u8 = undefined;
    resolveAt(@ptrFromInt(args.arg[1]), &zbuf);
    const buf: [*]u8 = @ptrFromInt(args.arg[2]);
    const bufsz = args.arg[3];
    var got: u64 = 0;
    const rc = vfs_if.readlink(@ptrCast(&zbuf), buf, bufsz, &got);
    return if (rc != 0) -@as(i64, rc) else @intCast(got);
}

fn sysFtruncate(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const pid = sched_if.current();
    const fd: u32 = @intCast(args.arg[0]);
    s_lock.lock();
    const tbl = tableOf(pid) orelse {
        s_lock.unlock();
        return -@as(i64, abi.EBADF);
    };
    if (fd >= MAX_FDS or tbl.fds[fd] == null or tbl.fds[fd].?.backing != .file) {
        s_lock.unlock();
        return -@as(i64, abi.EINVAL);
    }
    const of = tbl.fds[fd].?;
    s_lock.unlock();
    const rc = vfs_if.truncate(pathZ(of), args.arg[1]);
    return if (rc != 0) -@as(i64, rc) else 0;
}

fn sysZeroOk(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = args;
    _ = ctx;
    return 0; // fsync / fchmodat / fchownat / utimensat: accepted, no-op
}

fn fillStatfs(uptr: u64) void {
    if (uptr == 0) return;
    const p: [*]u8 = @ptrFromInt(uptr);
    var i: usize = 0;
    while (i < 120) : (i += 1) p[i] = 0;
    // aarch64 struct statfs: 8-byte fields.
    std.mem.writeInt(u64, p[0..8], 0xEF53, .little); // f_type (EXT2_SUPER_MAGIC)
    std.mem.writeInt(u64, p[8..16], 4096, .little); // f_bsize
    std.mem.writeInt(u64, p[16..24], 262144, .little); // f_blocks (1 GiB)
    std.mem.writeInt(u64, p[24..32], 131072, .little); // f_bfree
    std.mem.writeInt(u64, p[32..40], 131072, .little); // f_bavail
    std.mem.writeInt(u64, p[40..48], 65536, .little); // f_files
    std.mem.writeInt(u64, p[48..56], 32768, .little); // f_ffree
    std.mem.writeInt(u64, p[72..80], 255, .little); // f_namelen
    std.mem.writeInt(u64, p[80..88], 4096, .little); // f_frsize
}

fn sysStatfs(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    var zbuf: [PATH_MAX]u8 = undefined;
    resolveAt(@ptrFromInt(args.arg[0]), &zbuf);
    var ino: u32 = 0;
    var ft: u8 = 0;
    if (vfs_if.resolve(@ptrCast(&zbuf), &ino, &ft) != 0) return -@as(i64, abi.ENOENT);
    fillStatfs(args.arg[1]);
    return 0;
}

fn sysFstatfs(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const fd: u32 = @intCast(args.arg[0]);
    if (fd >= MAX_FDS) return -@as(i64, abi.EBADF);
    fillStatfs(args.arg[1]);
    return 0;
}

fn sysMknodat(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    var zbuf: [PATH_MAX]u8 = undefined;
    resolveAt(@ptrFromInt(args.arg[1]), &zbuf);
    const mode: u16 = @truncate(args.arg[2]);
    const dev: u32 = @truncate(args.arg[3]);
    const ft: u8 = switch (mode & abi.EXT2_S_IFMT) {
        abi.EXT2_S_IFCHR => abi.EXT2_FT_CHRDEV,
        abi.EXT2_S_IFBLK => abi.EXT2_FT_BLKDEV,
        abi.EXT2_S_IFIFO => abi.EXT2_FT_FIFO,
        abi.EXT2_S_IFSOCK => abi.EXT2_FT_SOCK,
        else => abi.EXT2_FT_REG_FILE,
    };
    const rc = vfs_if.mknod(@ptrCast(&zbuf), mode, ft, dev);
    return if (rc != 0) -@as(i64, rc) else 0;
}

// --- cwd -----------------------------------------------------

fn sysGetcwd(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const buf: [*]u8 = @ptrFromInt(args.arg[0]);
    const size: usize = @intCast(args.arg[1]);
    s_lock.lock();
    defer s_lock.unlock();
    const tbl = tableOf(sched_if.current()) orelse return -@as(i64, abi.EINVAL);
    var len: usize = 0;
    while (len < PATH_MAX and tbl.cwd[len] != 0) len += 1;
    if (len == 0) {
        tbl.cwd[0] = '/';
        len = 1;
    }
    if (len + 1 > size) return -@as(i64, abi.ERANGE);
    var i: usize = 0;
    while (i < len) : (i += 1) buf[i] = tbl.cwd[i];
    buf[len] = 0;
    return @intCast(len + 1); // Linux getcwd returns the length including the NUL
}

fn setCwdTo(pid: u32, zp: [*:0]const u8) i64 {
    var ino: u32 = 0;
    var ft: u8 = 0;
    const rc = vfs_if.resolve(zp, &ino, &ft);
    if (rc != 0) return -@as(i64, rc);
    if (ft != abi.EXT2_FT_DIR) return -@as(i64, abi.ENOTDIR);
    s_lock.lock();
    defer s_lock.unlock();
    const tbl = tableOf(pid) orelse return -@as(i64, abi.EINVAL);
    copyPath(&tbl.cwd, zp);
    return 0;
}

fn sysChdir(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    var zbuf: [PATH_MAX]u8 = undefined;
    resolvePath(sched_if.current(), @ptrFromInt(args.arg[0]), &zbuf);
    return setCwdTo(sched_if.current(), @ptrCast(&zbuf));
}

fn sysFchdir(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const pid = sched_if.current();
    const fd: u32 = @intCast(args.arg[0]);
    s_lock.lock();
    const tbl = tableOf(pid) orelse {
        s_lock.unlock();
        return -@as(i64, abi.EBADF);
    };
    if (fd >= MAX_FDS or tbl.fds[fd] == null or tbl.fds[fd].?.backing != .file) {
        s_lock.unlock();
        return -@as(i64, abi.EBADF);
    }
    var pbuf: [PATH_MAX]u8 = undefined;
    copyPath(&pbuf, pathZ(tbl.fds[fd].?));
    s_lock.unlock();
    return setCwdTo(pid, @ptrCast(&pbuf));
}

// --- pipe2 -------------------------------------------------

fn sysPipe2(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const ufds: [*]i32 = @ptrFromInt(args.arg[0]);
    const flags: u32 = @truncate(args.arg[1]);
    const pid = sched_if.current();

    s_lock.lock();
    defer s_lock.unlock();
    const tbl = tableOf(pid) orelse return -@as(i64, abi.EBADF);

    const pidx = allocPipe() orelse return -@as(i64, abi.ENFILE);
    const rd = allocOpen() orelse {
        s_pipes[pidx] = .{};
        return -@as(i64, abi.ENFILE);
    };
    // Claim rd's slot immediately -- allocOpen() picks the first
    // backing==.none/refcount==0 slot, so it would hand out this same one
    // again for wr otherwise.
    rd.* = .{ .backing = .pipe, .refcount = 1, .offset = pidx, .flags = flags & O_NONBLOCK };
    const wr = allocOpen() orelse {
        rd.* = .{};
        s_pipes[pidx] = .{};
        return -@as(i64, abi.ENFILE);
    };
    wr.* = .{ .backing = .pipe, .refcount = 1, .offset = pidx, .flags = 1 | (flags & O_NONBLOCK) };

    const s0 = lowestFreeFd(tbl) orelse {
        rd.* = .{};
        wr.* = .{};
        s_pipes[pidx] = .{};
        return -@as(i64, abi.EMFILE);
    };
    tbl.fds[s0] = rd;
    const s1 = lowestFreeFd(tbl) orelse {
        tbl.fds[s0] = null;
        rd.* = .{};
        wr.* = .{};
        s_pipes[pidx] = .{};
        return -@as(i64, abi.EMFILE);
    };
    tbl.fds[s1] = wr;

    ufds[0] = @intCast(s0);
    ufds[1] = @intCast(s1);
    return 0;
}

pub fn main(boot_info_ptr: *anyopaque) void {
    _ = boot_info_ptr;
    _ = sc_if.register(abi.SYS_read, &sysRead, null);
    _ = sc_if.register(abi.SYS_write, &sysWrite, null);
    _ = sc_if.register(abi.SYS_openat, &sysOpenat, null);
    _ = sc_if.register(abi.SYS_close, &sysClose, null);
    _ = sc_if.register(abi.SYS_lseek, &sysLseek, null);
    _ = sc_if.register(abi.SYS_dup, &sysDup, null);
    _ = sc_if.register(abi.SYS_writev, &sysWritev, null);
    _ = sc_if.register(abi.SYS_readv, &sysReadv, null);
    _ = sc_if.register(abi.SYS_fstat, &sysFstat, null);
    _ = sc_if.register(abi.SYS_newfstatat, &sysNewfstatat, null);
    _ = sc_if.register(abi.SYS_getdents64, &sysGetdents64, null);
    _ = sc_if.register(abi.SYS_fcntl, &sysFcntl, null);
    _ = sc_if.register(abi.SYS_dup3, &sysDup3, null);
    _ = sc_if.register(abi.SYS_faccessat, &sysFaccessat, null);
    _ = sc_if.register(abi.SYS_faccessat2, &sysFaccessat2, null);
    _ = sc_if.register(abi.SYS_ioctl, &sysIoctl, null);
    _ = sc_if.register(abi.SYS_mkdirat, &sysMkdirat, null);
    _ = sc_if.register(abi.SYS_unlinkat, &sysUnlinkat, null);
    _ = sc_if.register(abi.SYS_renameat, &sysRenameat, null);
    _ = sc_if.register(abi.SYS_symlinkat, &sysSymlinkat, null);
    _ = sc_if.register(abi.SYS_readlinkat, &sysReadlinkat, null);
    _ = sc_if.register(abi.SYS_ftruncate, &sysFtruncate, null);
    _ = sc_if.register(abi.SYS_fsync, &sysZeroOk, null);
    _ = sc_if.register(abi.SYS_fchmodat, &sysZeroOk, null);
    _ = sc_if.register(abi.SYS_fchownat, &sysZeroOk, null);
    _ = sc_if.register(abi.SYS_utimensat, &sysZeroOk, null);
    _ = sc_if.register(abi.SYS_getcwd, &sysGetcwd, null);
    _ = sc_if.register(abi.SYS_chdir, &sysChdir, null);
    _ = sc_if.register(abi.SYS_fchdir, &sysFchdir, null);
    _ = sc_if.register(abi.SYS_pipe2, &sysPipe2, null);
    _ = sc_if.register(abi.SYS_statfs, &sysStatfs, null);
    _ = sc_if.register(abi.SYS_fstatfs, &sysFstatfs, null);
    _ = sc_if.register(abi.SYS_mknodat, &sysMknodat, null);
    kernel_fmt.print(serial_if, "[fd] tables + rw/open/close/lseek/dup + stat/dents/fcntl/ioctl/*at + cwd + pipe2\n", .{});
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
