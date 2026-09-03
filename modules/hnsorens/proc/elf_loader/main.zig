//! Userspace ELF loader + the handful of process syscalls a first EL0
//! program needs. Exports `Elf` (category "elf").
//!
//! `load` reads a static AArch64 `ET_EXEC` binary through the VFS, then:
//!   1. `mmu.copy` of the bootloader's TTBR0 identity map -> a fresh root.
//!      The copy keeps device MMIO and the RAM identity map reachable
//!      from EL1 while this process's TTBR0 is active (so a syscall
//!      handler can still poke the UART); none of it carries the EL0
//!      access bit, so the process itself can't touch it.
//!   2. each PT_LOAD segment mapped at its `p_vaddr` (the test program is
//!      linked at 64 GiB, well above the identity-mapped range, so it
//!      lands in empty page-table slots -- no clash with the identity
//!      map's 1 GiB blocks).
//!   3. a stack mapped at 128 GiB.
//!   4. `process.create_user_process` with the composed TTBR0, the ELF
//!      entry point and the stack top.
//!
//! `unload` reverses it: destroy the TCB, then free every frame and the
//! page tables.
//!
//! HendOS did this in `elfLoader.c` + `process_execvp`; this keeps just
//! the static-executable path and layers the process syscalls
//! (write/getpid/exit/sched_yield) on top since they share every
//! dependency.
const std = @import("std");
const abi = @import("abi");
const kernel_fmt = @import("kernel_fmt");
const spinlock = @import("spinlock");

pub const vfs_if = abi.importInterface(abi.Vfs);
pub const mmu_if = abi.importInterface(abi.Mmu);
pub const pmm_if = abi.importInterface(abi.Pmm);
pub const process_if = abi.importInterface(abi.Process);
pub const fd_if = abi.importInterface(abi.Fd);
pub const sched_if = abi.importInterface(abi.Scheduler);
pub const sc_if = abi.importInterface(abi.Syscalls);
pub const serial_if = abi.importInterface(abi.Serial);

const ROOT_MASK: u64 = 0x0000_FFFF_FFFF_F000; // strip the ASID bits off a TTBR0 value

const PAGE_SIZE: u64 = 4096;
const USER_STACK_BASE: u64 = 0x0000_2000_0000_0000; // 128 GiB
const USER_STACK_PAGES: u64 = 16;
const KSTACK_PAGES: u32 = 8;

const ET_EXEC: u16 = 2;
const EM_AARCH64: u16 = 183;
const PF_X: u32 = 1;
const PF_W: u32 = 2;

const MAX_IMAGES = 32;
const MAX_SEGS = 8;

const SegRec = struct { phys: u64 = 0, pages: u64 = 0 };

const Image = struct {
    in_use: bool = false,
    pid: u32 = 0,
    uroot: u64 = 0,
    stack_phys: u64 = 0,
    n_segs: usize = 0,
    segs: [MAX_SEGS]SegRec = [_]SegRec{.{}} ** MAX_SEGS,
};

var s_images: [MAX_IMAGES]Image = [_]Image{.{}} ** MAX_IMAGES;
var s_next_asid: u16 = 1;
var s_lock: spinlock.SpinLock = .{};

fn orderForBytes(size: u64) u8 {
    const pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    var order: u8 = 0;
    while ((@as(u64, 1) << @as(u6, @intCast(order))) < pages) : (order += 1) {}
    return order;
}

fn hhdm(phys: u64) [*]u8 {
    return @ptrFromInt(phys + abi.HHDM_OFFSET);
}

fn allocFrames(bytes: u64, out_phys: *u64, out_pages: *u64) c_int {
    const order = orderForBytes(bytes);
    const st = pmm_if.alloc_page(order, out_phys);
    if (st != 0) return st;
    out_pages.* = @as(u64, 1) << @as(u6, @intCast(order));
    @memset(hhdm(out_phys.*)[0 .. out_pages.* * PAGE_SIZE], 0);
    return 0;
}

fn freeImageParts(uroot: u64, segs: []const SegRec, stack_phys: u64) void {
    for (segs) |s| {
        if (s.phys != 0) _ = pmm_if.release(s.phys);
    }
    if (stack_phys != 0) _ = pmm_if.release(stack_phys);
    if (uroot != 0) _ = mmu_if.free(uroot);
}

// --- load ----------------------------------------------------------

pub fn load(path: [*:0]const u8, out_pid: *u32) callconv(.c) c_int {
    var st: abi.Ext2Stat = .{};
    if (vfs_if.stat(path, &st) != 0) return abi.ENOENT;
    if (st.size == 0 or st.size > 16 * 1024 * 1024) return abi.ENOEXEC;
    if ((st.mode & abi.EXT2_S_IFMT) != abi.EXT2_S_IFREG) return abi.ENOEXEC;

    // Whole file into a scratch buffer.
    var file_phys: u64 = undefined;
    var file_pages: u64 = undefined;
    if (allocFrames(st.size, &file_phys, &file_pages) != 0) return abi.ENOMEM;
    defer _ = pmm_if.release(file_phys);

    var nread: u64 = 0;
    if (vfs_if.read(path, 0, hhdm(file_phys), st.size, &nread) != 0 or nread != st.size) {
        return abi.EIO;
    }

    const buf = hhdm(file_phys)[0..st.size];
    if (buf.len < @sizeOf(std.elf.Elf64_Ehdr)) return abi.ENOEXEC;
    const ehdr: *const std.elf.Elf64_Ehdr = @ptrCast(@alignCast(buf.ptr));
    if (!std.mem.eql(u8, ehdr.e_ident[0..4], std.elf.MAGIC)) return abi.ENOEXEC;
    if (ehdr.e_ident[4] != 2) return abi.ENOEXEC; // ELFCLASS64
    // e_type / e_machine read raw -- some std.elf versions type these as
    // enums, which would not compare against a plain integer.
    const e_type = std.mem.readInt(u16, buf.ptr[16..18], .little);
    const e_machine = std.mem.readInt(u16, buf.ptr[18..20], .little);
    if (e_type != ET_EXEC or e_machine != EM_AARCH64) return abi.ENOEXEC;
    if (ehdr.e_entry == 0 or ehdr.e_phnum == 0) return abi.ENOEXEC;
    if (ehdr.e_phoff + @as(u64, ehdr.e_phnum) * @sizeOf(std.elf.Elf64_Phdr) > st.size) return abi.ENOEXEC;

    // Fresh user address space = a private copy of the kernel identity map.
    var kroot: u64 = 0;
    if (mmu_if.get_user_ctx(&kroot) != 0 or kroot == 0) return abi.EIO;
    var uroot: u64 = 0;
    if (mmu_if.copy(kroot, &uroot) != 0) return abi.ENOMEM;

    var segs: [MAX_SEGS]SegRec = [_]SegRec{.{}} ** MAX_SEGS;
    var n_segs: usize = 0;

    const phdrs: [*]const std.elf.Elf64_Phdr = @ptrCast(@alignCast(buf.ptr + @as(usize, @intCast(ehdr.e_phoff))));

    // The whole program image is loaded as one contiguous block spanning
    // the page-aligned range of every PT_LOAD segment, so segments that
    // are packed sub-page-tight (as lld's default AArch64 layout does)
    // still land correctly. Protection is a union: read-only unless some
    // segment is writable, executable if some segment is.
    var lo: u64 = ~@as(u64, 0);
    var hi: u64 = 0;
    var all_ro = true;
    var any_x = false;
    var have_load = false;
    var pi: usize = 0;
    while (pi < ehdr.e_phnum) : (pi += 1) {
        const ph = phdrs[pi];
        if (ph.p_type != std.elf.PT_LOAD or ph.p_memsz == 0) continue;
        if (ph.p_filesz > ph.p_memsz or ph.p_offset + ph.p_filesz > st.size) {
            freeImageParts(uroot, &.{}, 0);
            return abi.ENOEXEC;
        }
        const s = ph.p_vaddr & ~@as(u64, PAGE_SIZE - 1);
        const e = (ph.p_vaddr + ph.p_memsz + PAGE_SIZE - 1) & ~@as(u64, PAGE_SIZE - 1);
        if (s < lo) lo = s;
        if (e > hi) hi = e;
        if (ph.p_flags & PF_W != 0) all_ro = false;
        if (ph.p_flags & PF_X != 0) any_x = true;
        have_load = true;
    }
    if (!have_load or hi <= lo or hi - lo > 64 * 1024 * 1024) {
        freeImageParts(uroot, &.{}, 0);
        return abi.ENOEXEC;
    }

    const span = hi - lo;
    var img_phys: u64 = undefined;
    var img_pages: u64 = undefined;
    if (allocFrames(span, &img_phys, &img_pages) != 0) {
        freeImageParts(uroot, &.{}, 0);
        return abi.ENOMEM;
    }
    pi = 0;
    while (pi < ehdr.e_phnum) : (pi += 1) {
        const ph = phdrs[pi];
        if (ph.p_type != std.elf.PT_LOAD or ph.p_memsz == 0 or ph.p_filesz == 0) continue;
        @memcpy(hhdm(img_phys)[ph.p_vaddr - lo ..][0..ph.p_filesz], buf[ph.p_offset..][0..ph.p_filesz]);
    }

    var flags: u64 = abi.MMU_USER;
    if (all_ro) flags |= abi.MMU_RO;
    if (!any_x) flags |= abi.MMU_NO_EXEC;
    if (mmu_if.map(uroot, lo, img_phys, span / PAGE_SIZE, .ps_4kb, flags) != 0) {
        _ = pmm_if.release(img_phys);
        freeImageParts(uroot, &.{}, 0);
        return abi.ENOMEM;
    }
    segs[0] = .{ .phys = img_phys, .pages = img_pages };
    n_segs = 1;

    // User stack.
    var stack_phys: u64 = undefined;
    var stack_pages: u64 = undefined;
    if (allocFrames(USER_STACK_PAGES * PAGE_SIZE, &stack_phys, &stack_pages) != 0) {
        freeImageParts(uroot, segs[0..n_segs], 0);
        return abi.ENOMEM;
    }
    if (mmu_if.map(uroot, USER_STACK_BASE, stack_phys, USER_STACK_PAGES, .ps_4kb, abi.MMU_USER | abi.MMU_NO_EXEC) != 0) {
        freeImageParts(uroot, segs[0..n_segs], stack_phys);
        return abi.ENOMEM;
    }
    const user_sp = USER_STACK_BASE + USER_STACK_PAGES * PAGE_SIZE;

    s_lock.lock();
    const asid: u16 = blk: {
        const a = s_next_asid & 0xFF;
        s_next_asid += 1;
        break :blk if (a == 0) 1 else a;
    };
    const img_slot = for (&s_images) |*im| {
        if (!im.in_use) break im;
    } else {
        s_lock.unlock();
        freeImageParts(uroot, segs[0..n_segs], stack_phys);
        return abi.ENOMEM;
    };
    s_lock.unlock();

    const ttbr0 = (@as(u64, asid) << 48) | uroot;

    var pid: u32 = 0;
    const rc = process_if.create_user_process("elf", ttbr0, ehdr.e_entry, user_sp, KSTACK_PAGES, 0, &pid);
    if (rc != 0) {
        freeImageParts(uroot, segs[0..n_segs], stack_phys);
        return rc;
    }

    img_slot.* = .{
        .in_use = true,
        .pid = pid,
        .uroot = uroot,
        .stack_phys = stack_phys,
        .n_segs = n_segs,
        .segs = segs,
    };
    // fresh process -> stdin/stdout/stderr on the console.
    _ = fd_if.open_defaults(pid);
    out_pid.* = pid;
    return 0;
}

pub fn unload(pid: u32) callconv(.c) c_int {
    s_lock.lock();
    const img = for (&s_images) |*im| {
        if (im.in_use and im.pid == pid) break im;
    } else {
        s_lock.unlock();
        return abi.EINVAL;
    };
    s_lock.unlock();

    const rc = process_if.destroy(pid);
    if (rc != 0) return rc; // EBUSY while running

    _ = fd_if.clear_table(pid); // idempotent -- exit() may already have
    freeImageParts(img.uroot, img.segs[0..img.n_segs], img.stack_phys);
    img.* = .{};
    return 0;
}

// --- process-lifecycle syscalls + the process tree ------------------
//
// getpid / getppid / sched_yield / exit / exit_group / wait4, and clone
// (== fork). read/write/openat/close/lseek/dup are the fd module's.
//
// fork = mmu.fork of the caller's address space (an eager, independent
// copy of every page -- not CoW), process.create_forked_process (a TCB
// whose context erets to EL0 from the caller's trap frame with x0 = 0),
// fd.fork_table, then admit. wait4 finds a zombie child, writes a
// Linux-style status, and reaps it (frees the space via mmu.free_all and
// the TCB). A parent that exits reparents its live children to pid 0
// (reparent-to-init needs an init that actually reaps).

const KSTACK_PAGES_FORK: u32 = 8;

var s_waiters: [16]u32 = [_]u32{0} ** 16;
var s_proc_lock: spinlock.SpinLock = .{};

fn nextForkAsid() u16 {
    s_lock.lock();
    defer s_lock.unlock();
    const a = s_next_asid & 0xFF;
    s_next_asid += 1;
    return if (a == 0) 1 else a;
}

pub fn addWaiter(pid: u32) void {
    s_proc_lock.lock();
    defer s_proc_lock.unlock();
    for (&s_waiters) |*w| {
        if (w.* == pid) return;
    }
    for (&s_waiters) |*w| {
        if (w.* == 0) {
            w.* = pid;
            return;
        }
    }
}

pub fn removeWaiter(pid: u32) void {
    s_proc_lock.lock();
    defer s_proc_lock.unlock();
    for (&s_waiters) |*w| {
        if (w.* == pid) w.* = 0;
    }
}

pub fn isWaiting(pid: u32) bool {
    s_proc_lock.lock();
    defer s_proc_lock.unlock();
    for (s_waiters) |w| {
        if (w == pid) return true;
    }
    return false;
}

fn reparentChildren(ppid: u32, new_parent: u32) void {
    var pids: [64]u32 = undefined;
    var n: u32 = 0;
    _ = process_if.list(&pids, pids.len, &n);
    for (pids[0..n]) |p| {
        var info: abi.ProcessInfo = .{};
        if (process_if.get_info(p, &info) == 0 and info.parent == ppid) {
            _ = process_if.set_parent(p, new_parent);
        }
    }
}

fn reapChild(pid: u32) void {
    var info: abi.ProcessInfo = .{};
    if (process_if.get_info(pid, &info) == 0) {
        _ = fd_if.clear_table(pid);
        if (info.address_space != 0) _ = mmu_if.free_all(info.address_space & ROOT_MASK);
    }
    _ = process_if.destroy(pid);
}

fn sysGetpid(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = args;
    _ = ctx;
    return @intCast(sched_if.current());
}

fn sysGetppid(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = args;
    _ = ctx;
    var info: abi.ProcessInfo = .{};
    if (process_if.get_info(sched_if.current(), &info) != 0) return 0;
    return @intCast(info.parent);
}

fn sysSchedYield(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = args;
    _ = ctx;
    sched_if.yield();
    return 0;
}

fn sysExit(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const me = sched_if.current();
    if (me == 0) {
        sched_if.exit_current();
        return 0;
    }
    _ = process_if.set_exit_code(me, @truncate(@as(i64, @bitCast(args.arg[0]))));
    _ = fd_if.clear_table(me);
    reparentChildren(me, 0);

    var info: abi.ProcessInfo = .{};
    if (process_if.get_info(me, &info) == 0 and info.parent != 0 and isWaiting(info.parent)) {
        _ = sched_if.wake(info.parent);
    }

    sched_if.exit_current();
    return 0; // not reached
}

fn sysClone(frame: *abi.TrapFrame, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const parent = sched_if.current();
    if (parent == 0) return -@as(i64, abi.EINVAL);

    var pinfo: abi.ProcessInfo = .{};
    if (process_if.get_info(parent, &pinfo) != 0 or pinfo.address_space == 0) return -@as(i64, abi.EINVAL);

    var child_root: u64 = 0;
    if (mmu_if.fork(pinfo.address_space & ROOT_MASK, &child_root) != 0) return -@as(i64, abi.ENOMEM);

    const child_ttbr0 = (@as(u64, nextForkAsid()) << 48) | child_root;

    var child_pid: u32 = 0;
    const rc = process_if.create_forked_process("fork", child_ttbr0, parent, frame, KSTACK_PAGES_FORK, pinfo.priority, &child_pid);
    if (rc != 0) {
        _ = mmu_if.free_all(child_root);
        return -@as(i64, rc);
    }

    _ = fd_if.fork_table(parent, child_pid);
    _ = sched_if.admit(child_pid);
    return @intCast(child_pid); // parent gets the pid; the child's own context yields 0
}

fn sysWait4(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const me = sched_if.current();
    if (me == 0) return -@as(i64, abi.EINVAL);

    const wpid: i64 = @bitCast(args.arg[0]);
    const status_ptr = args.arg[1];

    while (true) {
        var pids: [64]u32 = undefined;
        var n: u32 = 0;
        _ = process_if.list(&pids, pids.len, &n);

        var any_child = false;
        var zombie: u32 = 0;
        for (pids[0..n]) |p| {
            var info: abi.ProcessInfo = .{};
            if (process_if.get_info(p, &info) != 0 or info.parent != me) continue;
            if (wpid > 0 and p != @as(u32, @intCast(wpid))) continue;
            any_child = true;
            if (info.state == .zombie) {
                zombie = p;
                break;
            }
        }

        if (zombie != 0) {
            var zi: abi.ProcessInfo = .{};
            _ = process_if.get_info(zombie, &zi);
            if (status_ptr != 0) {
                const sp: *i32 = @ptrFromInt(status_ptr);
                sp.* = (zi.exit_code & 0xFF) << 8;
            }
            reapChild(zombie);
            return @intCast(zombie);
        }
        if (!any_child) return -@as(i64, abi.ECHILD);

        addWaiter(me);
        _ = sched_if.block();
        removeWaiter(me);
    }
}

pub fn main(boot_info_ptr: *anyopaque) void {
    _ = boot_info_ptr;
    _ = sc_if.register(abi.SYS_getpid, &sysGetpid, null);
    _ = sc_if.register(abi.SYS_getppid, &sysGetppid, null);
    _ = sc_if.register(abi.SYS_sched_yield, &sysSchedYield, null);
    _ = sc_if.register(abi.SYS_exit, &sysExit, null);
    _ = sc_if.register(abi.SYS_exit_group, &sysExit, null);
    _ = sc_if.register(abi.SYS_wait4, &sysWait4, null);
    _ = sc_if.register_raw(abi.SYS_clone, &sysClone, null);
    kernel_fmt.print(serial_if, "[elf_loader] ready; process syscalls + tree\n", .{});
}

comptime {
    abi.exportInterface("loader", abi.Elf, .{
        .load = load,
        .unload = unload,
    });
}

comptime {
    _ = @import("test.zig");
}
