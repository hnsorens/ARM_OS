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
pub const signal_if = abi.importInterface(abi.Signal);
pub const serial_if = abi.importInterface(abi.Serial);

const ROOT_MASK: u64 = 0x0000_FFFF_FFFF_F000; // strip the ASID bits off a TTBR0 value

const PAGE_SIZE: u64 = 4096;
const USER_STACK_BASE: u64 = 0x0000_2000_0000_0000; // 128 GiB
const USER_STACK_PAGES: u64 = 128; // 512 KiB -- eager; demand-grown stack is a later fault-handler job
const KSTACK_PAGES: u32 = 8;

// auxv keys (AArch64/Linux) emitted onto the initial stack. musl reads
// AT_PAGESZ (mandatory -- no fallback), AT_RANDOM (stack canary), and
// AT_PHDR/PHENT/PHNUM (needed once a binary has real __thread state).
const AT_NULL: u64 = 0;
const AT_PHDR: u64 = 3;
const AT_PHENT: u64 = 4;
const AT_PHNUM: u64 = 5;
const AT_PAGESZ: u64 = 6;
const AT_BASE: u64 = 7;
const AT_FLAGS: u64 = 8;
const AT_ENTRY: u64 = 9;
const AT_UID: u64 = 11;
const AT_EUID: u64 = 12;
const AT_GID: u64 = 13;
const AT_EGID: u64 = 14;
const AT_HWCAP: u64 = 16;
const AT_CLKTCK: u64 = 17;
const AT_SECURE: u64 = 23;
const AT_RANDOM: u64 = 25;
const AT_EXECFN: u64 = 31;

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
    /// True for a `clone()` child whose address space is a `mmu.fork`
    /// deep copy (freed with `free_all`, no discrete seg/stack records).
    /// Cleared once `execve` rebuilds it as a `map`-built image.
    forked: bool = false,
};

var s_images: [MAX_IMAGES]Image = [_]Image{.{}} ** MAX_IMAGES;
var s_next_asid: u16 = 1;
var s_lock: spinlock.SpinLock = .{};

// The kernel identity-map root, snapshotted in main() while the kernel's
// own TTBR0 is still active. buildImage must copy *this*, not "whatever
// TTBR0 currently is" -- during execve the caller's user space is active.
var s_kernel_root: u64 = 0;

fn kernelRoot() u64 {
    if (s_kernel_root != 0) return s_kernel_root;
    var r: u64 = 0;
    _ = mmu_if.get_user_ctx(&r);
    return r;
}

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

// --- image building (shared by load + execve) --------------------

const Built = struct {
    uroot: u64 = 0,
    entry: u64 = 0,
    img_phys: u64 = 0,
    img_pages: u64 = 0,
    stack_phys: u64 = 0,
    stack_pages: u64 = 0,
    // program-header location in the user image, for AT_PHDR/PHENT/PHNUM.
    phdr: u64 = 0,
    phent: u64 = 0,
    phnum: u64 = 0,
};

// Tiny xorshift for AT_RANDOM's 16 bytes -- not a security boundary yet
// (no getrandom, no real entropy), just enough that musl's stack canary
// isn't a fixed constant across processes.
var s_rng: u64 = 0x9E3779B97F4A7C15;

fn fillRandom(dst: []u8) void {
    var i: usize = 0;
    while (i < dst.len) {
        var x = s_rng;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        s_rng = x;
        var j: usize = 0;
        while (j < 8 and i < dst.len) : (j += 1) {
            dst[i] = @truncate(x >> @as(u6, @intCast(j * 8)));
            i += 1;
        }
    }
}

/// Builds the SysV/AArch64 initial process stack in `stack_phys` (written
/// through the HHDM alias, so it is independent of which TTBR0 is live):
///
///   [argc][argv..][NULL][envp..][NULL][auxv pairs..][AT_NULL,0][strings][rand16]
///
/// `strbuf` is a packed run of NUL-terminated strings; `argv_off`/`envp_off`
/// give each string's offset within it (their lengths are argc/envc).
/// Returns the user-visible SP (16-aligned, pointing at argc), or 0 if it
/// would not fit the mapped stack.
fn buildUserStack(
    stack_phys: u64,
    b: *const Built,
    strbuf: []const u8,
    argv_off: []const u32,
    envp_off: []const u32,
    execfn_off: u32,
) u64 {
    const argc = argv_off.len;
    const envc = envp_off.len;
    const hh = hhdm(stack_phys);
    const stack_bytes = USER_STACK_PAGES * PAGE_SIZE;

    const writeAt = struct {
        fn f(base: [*]u8, v_addr: u64, bytes: []const u8) void {
            @memcpy(base[v_addr - USER_STACK_BASE ..][0..bytes.len], bytes);
        }
    }.f;

    var sp = USER_SP_TOP;

    // 16 random bytes for AT_RANDOM, at the very top.
    sp -= 16;
    const rand_v = sp;
    var randbytes: [16]u8 = undefined;
    fillRandom(&randbytes);
    writeAt(hh, rand_v, &randbytes);

    // The argv/envp/execfn strings.
    if (strbuf.len + 512 > stack_bytes) return 0;
    sp -= strbuf.len;
    const strings_v = sp;
    writeAt(hh, strings_v, strbuf);

    const aux = [_][2]u64{
        .{ AT_PHDR, b.phdr },
        .{ AT_PHENT, b.phent },
        .{ AT_PHNUM, b.phnum },
        .{ AT_PAGESZ, PAGE_SIZE },
        .{ AT_ENTRY, b.entry },
        .{ AT_BASE, 0 },
        .{ AT_FLAGS, 0 },
        .{ AT_UID, 0 },
        .{ AT_EUID, 0 },
        .{ AT_GID, 0 },
        .{ AT_EGID, 0 },
        .{ AT_SECURE, 0 },
        .{ AT_HWCAP, 0 },
        .{ AT_CLKTCK, 100 },
        .{ AT_RANDOM, rand_v },
        .{ AT_EXECFN, strings_v + execfn_off },
    };

    const ptr_slots = 1 + argc + 1 + envc + 1 + (aux.len + 1) * 2;
    sp = (sp - ptr_slots * 8) & ~@as(u64, 0xF);
    if (USER_SP_TOP - sp + 16 > stack_bytes) return 0;

    const vp: [*]u64 = @ptrCast(@alignCast(hh + (sp - USER_STACK_BASE)));
    var k: usize = 0;
    vp[k] = argc;
    k += 1;
    for (argv_off) |o| {
        vp[k] = strings_v + o;
        k += 1;
    }
    vp[k] = 0;
    k += 1;
    for (envp_off) |o| {
        vp[k] = strings_v + o;
        k += 1;
    }
    vp[k] = 0;
    k += 1;
    for (aux) |pair| {
        vp[k] = pair[0];
        k += 1;
        vp[k] = pair[1];
        k += 1;
    }
    vp[k] = AT_NULL;
    k += 1;
    vp[k] = 0;
    return sp;
}

fn freeBuilt(b: *const Built) void {
    if (b.img_phys != 0) _ = pmm_if.release(b.img_phys);
    if (b.stack_phys != 0) _ = pmm_if.release(b.stack_phys);
    if (b.uroot != 0) _ = mmu_if.free(b.uroot);
}

/// Reads `path`, parses the static AArch64 ET_EXEC, and constructs a
/// fresh user address space (identity-map copy + the PT_LOAD span mapped
/// as one block + a 128 GiB stack). On failure everything it allocated is
/// freed and an errno is returned; on success `out` is filled and the
/// caller owns the resources (attach to a process, or `freeBuilt`).
fn buildImage(path: [*:0]const u8, out: *Built) c_int {
    var st: abi.Ext2Stat = .{};
    if (vfs_if.stat(path, &st) != 0) return abi.ENOENT;
    if (st.size == 0 or st.size > 16 * 1024 * 1024) return abi.ENOEXEC;
    if ((st.mode & abi.EXT2_S_IFMT) != abi.EXT2_S_IFREG) return abi.ENOEXEC;

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
    const kroot = kernelRoot();
    if (kroot == 0) return abi.EIO;
    var uroot: u64 = 0;
    if (mmu_if.copy(kroot, &uroot) != 0) return abi.ENOMEM;

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
    var phdr_v: u64 = 0;
    var pi: usize = 0;
    while (pi < ehdr.e_phnum) : (pi += 1) {
        const ph = phdrs[pi];
        if (ph.p_type == std.elf.PT_PHDR) {
            phdr_v = ph.p_vaddr;
            continue;
        }
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

    // User stack.
    var stack_phys: u64 = undefined;
    var stack_pages: u64 = undefined;
    if (allocFrames(USER_STACK_PAGES * PAGE_SIZE, &stack_phys, &stack_pages) != 0) {
        _ = pmm_if.release(img_phys);
        _ = mmu_if.free(uroot);
        return abi.ENOMEM;
    }
    if (mmu_if.map(uroot, USER_STACK_BASE, stack_phys, USER_STACK_PAGES, .ps_4kb, abi.MMU_USER | abi.MMU_NO_EXEC) != 0) {
        _ = pmm_if.release(img_phys);
        _ = pmm_if.release(stack_phys);
        _ = mmu_if.free(uroot);
        return abi.ENOMEM;
    }

    // Program headers: prefer PT_PHDR's vaddr; else they sit right after
    // the ELF header in the first mapped page (lld's static layout).
    if (phdr_v == 0 and ehdr.e_phoff < span) phdr_v = lo + ehdr.e_phoff;

    out.* = .{
        .uroot = uroot,
        .entry = ehdr.e_entry,
        .img_phys = img_phys,
        .img_pages = img_pages,
        .stack_phys = stack_phys,
        .stack_pages = stack_pages,
        .phdr = phdr_v,
        .phent = @sizeOf(std.elf.Elf64_Phdr),
        .phnum = ehdr.e_phnum,
    };
    return 0;
}

const USER_SP_TOP: u64 = USER_STACK_BASE + USER_STACK_PAGES * PAGE_SIZE;

// --- load ----------------------------------------------------------

pub fn load(path: [*:0]const u8, out_pid: *u32) callconv(.c) c_int {
    var b: Built = .{};
    const rc = buildImage(path, &b);
    if (rc != 0) return rc;

    // Initial stack: argv = [path], empty environment.
    var strbuf: [ARG_BUF]u8 = undefined;
    const pspan = std.mem.span(path);
    if (pspan.len + 1 > strbuf.len) {
        freeBuilt(&b);
        return abi.ENOMEM;
    }
    @memcpy(strbuf[0..pspan.len], pspan);
    strbuf[pspan.len] = 0;
    const argv_off = [_]u32{0};
    const user_sp = buildUserStack(b.stack_phys, &b, strbuf[0 .. pspan.len + 1], &argv_off, &[_]u32{}, 0);
    if (user_sp == 0) {
        freeBuilt(&b);
        return abi.ENOMEM;
    }

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
        freeBuilt(&b);
        return abi.ENOMEM;
    };
    s_lock.unlock();

    const ttbr0 = (@as(u64, asid) << 48) | b.uroot;

    var pid: u32 = 0;
    const rc2 = process_if.create_user_process("elf", ttbr0, b.entry, user_sp, KSTACK_PAGES, 0, &pid);
    if (rc2 != 0) {
        freeBuilt(&b);
        return rc2;
    }

    img_slot.* = .{
        .in_use = true,
        .pid = pid,
        .uroot = b.uroot,
        .stack_phys = b.stack_phys,
        .n_segs = 1,
        .segs = [_]SegRec{.{ .phys = b.img_phys, .pages = b.img_pages }} ++ [_]SegRec{.{}} ** (MAX_SEGS - 1),
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
    mmForget(pid, true); // mmu.free below won't touch anon leaf frames
    signal_if.forget(pid);
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
    const ok = process_if.get_info(pid, &info) == 0;
    if (ok) {
        _ = fd_if.clear_table(pid);
        signal_if.forget(pid);
    }

    // Free the address space the right way for how it was built.
    s_lock.lock();
    const img: ?*Image = for (&s_images) |*im| {
        if (im.in_use and im.pid == pid) break im;
    } else null;
    if (img) |im| {
        const forked = im.forked;
        const uroot = im.uroot;
        const segs = im.segs;
        const nseg = im.n_segs;
        const stk = im.stack_phys;
        im.* = .{};
        s_lock.unlock();
        if (forked) {
            mmForget(pid, false); // free_all frees the anon leaves too
            if (uroot != 0) _ = mmu_if.free_all(uroot & ROOT_MASK);
        } else {
            mmForget(pid, true); // mmu.free leaves the anon frames to us
            freeImageParts(uroot, segs[0..nseg], stk);
        }
    } else {
        s_lock.unlock();
        mmForget(pid, false);
        if (ok and info.address_space != 0) _ = mmu_if.free_all(info.address_space & ROOT_MASK);
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

/// Ends the current process with `code` (a full wait(2) status word -- so
/// a signal death passes `sig` in the low 7 bits, a normal exit passes
/// `(code & 0xff) << 8`; callers here mostly pass the raw exit code and
/// rely on wait4 to shift it, which is a small lie kept for simplicity).
/// Never returns.
fn terminateCurrent(code: i32) noreturn {
    const me = sched_if.current();
    if (me == 0) {
        sched_if.exit_current();
        unreachable;
    }
    _ = process_if.set_exit_code(me, code);
    _ = fd_if.clear_table(me);
    signal_if.forget(me);
    reparentChildren(me, 0);

    var info: abi.ProcessInfo = .{};
    if (process_if.get_info(me, &info) == 0 and info.parent != 0) {
        // raiseSignal already wakes the parent if it is blocked (e.g. in
        // wait4) -- do NOT also wake it here or it lands on the ready
        // queue twice.
        signal_if.raise(info.parent, 17); // SIGCHLD
    }

    sched_if.exit_current();
    unreachable;
}

fn sysExit(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    terminateCurrent(@truncate(@as(i64, @bitCast(args.arg[0]))));
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
    signal_if.fork_inherit(parent, child_pid);
    mmForkInherit(parent, child_pid);

    // Record the child so execve() can find and swap its image, and
    // teardown knows the space is a fork copy (free_all, not per-seg).
    s_lock.lock();
    for (&s_images) |*im| {
        if (!im.in_use) {
            im.* = .{ .in_use = true, .pid = child_pid, .uroot = child_root, .forked = true };
            break;
        }
    }
    s_lock.unlock();

    _ = sched_if.admit(child_pid);
    return @intCast(child_pid); // parent gets the pid; the child's own context yields 0
}

fn sysWait4(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const me = sched_if.current();
    if (me == 0) return -@as(i64, abi.EINVAL);

    const wpid: i64 = @bitCast(args.arg[0]);
    const status_ptr = args.arg[1];
    const WNOHANG: u64 = 1;
    const WUNTRACED: u64 = 2;
    const options = args.arg[2];

    while (true) {
        var pids: [64]u32 = undefined;
        var n: u32 = 0;
        _ = process_if.list(&pids, pids.len, &n);

        var any_child = false;
        var zombie: u32 = 0;
        var stopped: u32 = 0;
        for (pids[0..n]) |p| {
            var info: abi.ProcessInfo = .{};
            if (process_if.get_info(p, &info) != 0 or info.parent != me) continue;
            if (wpid > 0 and p != @as(u32, @intCast(wpid))) continue;
            any_child = true;
            if (info.state == .zombie) {
                zombie = p;
                break;
            }
            if (stopped == 0 and signal_if.is_stopped(p) != 0) stopped = p;
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
        if (stopped != 0 and options & WUNTRACED != 0) {
            // W_STOPCODE(SIGSTOP): 0x7f in the low byte, sig in the next.
            if (status_ptr != 0) @as(*i32, @ptrFromInt(status_ptr)).* = (19 << 8) | 0x7f;
            return @intCast(stopped); // reported, not reaped
        }
        if (!any_child) return -@as(i64, abi.ECHILD);
        if (options & WNOHANG != 0) return 0; // no reapable child right now

        // A pending signal breaks the wait: -ERESTARTSYS so the delivery
        // hook restarts wait4 for an SA_RESTART handler, else -EINTR.
        if (signal_if.has_pending(me) != 0) {
            signal_if.mark_restart(me, args.arg[0]);
            return -@as(i64, abi.ERESTARTSYS);
        }
        addWaiter(me);
        _ = sched_if.block();
        removeWaiter(me);
    }
}

// --- execve ------------------------------------------------------

const MAX_STRV = 32;
const ARG_BUF = 2048;

/// Copies a user NULL-terminated `char *[]` (at `uptr`, in the *current*
/// address space) into `buf`, appending NUL-terminated strings starting
/// at `*w`; records each string's offset in `off`. Returns the count.
fn copyStrv(uptr: u64, off: []u32, buf: []u8, w: *usize) usize {
    if (uptr == 0) return 0;
    const arr: [*]const u64 = @ptrFromInt(uptr);
    var n: usize = 0;
    while (n < off.len) : (n += 1) {
        const s_uptr = arr[n];
        if (s_uptr == 0) break;
        if (w.* >= buf.len - 1) break;
        const s: [*:0]const u8 = @ptrFromInt(s_uptr);
        off[n] = @intCast(w.*);
        var i: usize = 0;
        while (s[i] != 0 and w.* < buf.len - 1) : (i += 1) {
            buf[w.*] = s[i];
            w.* += 1;
        }
        buf[w.*] = 0;
        w.* += 1;
    }
    return n;
}

/// execve(path, argv, envp) -- replaces the caller's image in place,
/// keeping pid / parent / fd table. A raw handler: on success it rewrites
/// the trap frame so `eret` lands in the new program's `_start` with a
/// freshly built SysV initial stack, and never "returns" to the caller.
/// On failure (bad path, not an ET_EXEC, OOM) the caller is untouched and
/// gets `-errno`.
fn sysExecve(frame: *abi.TrapFrame, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const me = sched_if.current();
    if (me == 0) return -@as(i64, abi.EINVAL);
    if (frame.x[0] == 0) return -@as(i64, abi.EINVAL);
    const path: [*:0]const u8 = @ptrFromInt(frame.x[0]);

    // 1. Copy argv + envp strings out of the *old* address space (active).
    var strbuf: [ARG_BUF]u8 = undefined;
    var argv_off: [MAX_STRV]u32 = undefined;
    var envp_off: [MAX_STRV]u32 = undefined;
    var w: usize = 0;
    const argc = copyStrv(frame.x[1], &argv_off, &strbuf, &w);
    const envc = copyStrv(frame.x[2], &envp_off, &strbuf, &w);
    const strv_bytes = w;

    // 2. Build the new image. Fails safe -- old process untouched.
    var b: Built = .{};
    const rc = buildImage(path, &b);
    if (rc != 0) return -@as(i64, rc);

    // 3. Find the caller's image record; capture the old resources.
    s_lock.lock();
    const img = for (&s_images) |*im| {
        if (im.in_use and im.pid == me) break im;
    } else {
        s_lock.unlock();
        freeBuilt(&b);
        return -@as(i64, abi.EINVAL);
    };
    const old_uroot = img.uroot;
    const old_img_phys = img.segs[0].phys;
    const old_stack_phys = img.stack_phys;
    const old_forked = img.forked;
    s_lock.unlock();

    // 4. Build the new initial stack now, through the HHDM alias -- it is
    //    TTBR0-independent, so a failure here still leaves the caller
    //    completely intact (nothing has been committed yet).
    const execfn_off: u32 = if (argc > 0) argv_off[0] else 0;
    const user_sp = buildUserStack(b.stack_phys, &b, strbuf[0..strv_bytes], argv_off[0..argc], envp_off[0..envc], execfn_off);
    if (user_sp == 0) {
        freeBuilt(&b);
        return -@as(i64, abi.ENOMEM);
    }

    // 5. Commit: install the new address space.
    const new_ttbr0 = (@as(u64, nextForkAsid()) << 48) | b.uroot;
    _ = process_if.set_address_space(me, new_ttbr0);
    asm volatile ("msr ttbr0_el1, %[v]\nisb"
        :
        : [v] "r" (new_ttbr0),
        : .{ .memory = true });

    // 6. Rewrite the trap frame so eret enters the new program.
    for (&frame.x) |*r| r.* = 0;
    frame.elr = b.entry;
    frame.sp = user_sp; // exc_common restores SP_EL0 from here

    // 7. Adopt the new image; free the old one. execve keeps the pid but
    //    the whole address space is new -- drop and free the old
    //    process's anon mmap/brk frames (mmu.free won't).
    mmForget(me, true);
    signal_if.forget(me);
    _ = fd_if.on_execve(me); // close FD_CLOEXEC fds
    s_lock.lock();
    img.uroot = b.uroot;
    img.segs[0] = .{ .phys = b.img_phys, .pages = b.img_pages };
    img.stack_phys = b.stack_phys;
    img.n_segs = 1;
    img.forked = false; // now a map-built image
    s_lock.unlock();
    if (old_forked) {
        // The old space was a fork deep-copy: free_all (leaves + tables),
        // no discrete seg/stack frames to release.
        _ = mmu_if.free_all(old_uroot & ROOT_MASK);
    } else {
        _ = pmm_if.release(old_img_phys);
        _ = pmm_if.release(old_stack_phys);
        _ = mmu_if.free(old_uroot);
    }

    return 0;
}

// --- anonymous mmap + brk (per-pid, eager, own L0 slots) -------------
//
// mallocng (musl's allocator) gets its heap from mmap, not brk. Regions
// live in their own top-level (L0) slots so mmu.unmap prunes the whole
// L1/L2/L3 chain back cleanly (verified pmm-balanced); the image and
// stack keep their existing slots. brk is a private arena, also its own
// L0 slot.
//
// Known gaps (deferred to a real per-process VMA tree):
//   * munmap only releases a region unmapped whole (addr == its base).
//   * MAP_FIXED does no overlap check; no file-backed mmap.

const MMAP_BASE: u64 = 0x0000_5000_0000_0000; // 80 TiB, grows up (its own L0 slot)
const MMAP_LIMIT: u64 = 0x0000_5800_0000_0000;
const BRK_BASE: u64 = 0x0000_4800_0000_0000; // its own L0 slot
const BRK_MAX: u64 = 256 * 1024 * 1024;

const MAX_MM = 8; // concurrent user processes with anon mappings
const MAX_ANON = 32; // anon regions per process

const AnonRec = struct {
    in_use: bool = false,
    va: u64 = 0,
    phys: u64 = 0,
    pages: u64 = 0,
};

const MmState = struct {
    in_use: bool = false,
    pid: u32 = 0,
    // 0 == "not yet initialised"; mmStateLocked seeds these to
    // MMAP_BASE/BRK_BASE. Kept zero-default so `s_mm` lands in .bss.
    mmap_top: u64 = 0,
    brk_cur: u64 = 0,
    anon: [MAX_ANON]AnonRec = [_]AnonRec{.{}} ** MAX_ANON,
};

var s_mm: [MAX_MM]MmState = [_]MmState{.{}} ** MAX_MM;
var s_mm_lock: spinlock.SpinLock = .{};

fn userRootOf(pid: u32) u64 {
    var info: abi.ProcessInfo = .{};
    if (process_if.get_info(pid, &info) != 0) return 0;
    return info.address_space & ROOT_MASK;
}

fn mmStateLocked(pid: u32) ?*MmState {
    for (&s_mm) |*m| {
        if (m.in_use and m.pid == pid) return m;
    }
    for (&s_mm) |*m| {
        if (!m.in_use) {
            m.* = .{ .in_use = true, .pid = pid, .mmap_top = MMAP_BASE, .brk_cur = BRK_BASE };
            return m;
        }
    }
    return null;
}

/// fork: the child's address space is a `mmu.fork` deep-copy, so it
/// already contains the parent's anon mmap regions + brk arena at the
/// same VAs. Carry over the parent's `mmap_top` / `brk_cur` so the
/// child's allocator hands out fresh addresses past them (otherwise a
/// post-fork `malloc` that grows the heap re-`mmap`s an occupied VA and
/// gets EEXIST -> NULL). The per-region `anon[]` records are left to the
/// child to rebuild for its own new mmaps; teardown of a forked child
/// goes through `free_all`, which frees the copied leaves regardless.
pub fn mmForkInherit(parent: u32, child: u32) void {
    s_mm_lock.lock();
    defer s_mm_lock.unlock();
    const p = for (&s_mm) |*m| {
        if (m.in_use and m.pid == parent) break m;
    } else return; // parent never used mmap/brk
    for (&s_mm) |*m| {
        if (!m.in_use) {
            m.* = .{ .in_use = true, .pid = child, .mmap_top = p.mmap_top, .brk_cur = p.brk_cur };
            return;
        }
    }
}

fn protToMmuFlags(prot: u64) u64 {
    var f: u64 = abi.MMU_USER;
    if (prot & abi.PROT_WRITE == 0) f |= abi.MMU_RO;
    if (prot & abi.PROT_EXEC == 0) f |= abi.MMU_NO_EXEC;
    return f;
}

/// Free (free_frames=true) or just drop (false) a pid's anon bookkeeping.
/// true on teardown paths that use mmu.free (leaves leaf frames to us);
/// false where mmu.free_all already frees every leaf (fork/reap).
pub fn mmForget(pid: u32, free_frames: bool) void {
    s_mm_lock.lock();
    defer s_mm_lock.unlock();
    for (&s_mm) |*m| {
        if (!m.in_use or m.pid != pid) continue;
        if (free_frames) {
            for (&m.anon) |*r| {
                if (r.in_use) _ = pmm_if.release(r.phys);
            }
        }
        m.* = .{};
        return;
    }
}

pub fn mmRegionCount(pid: u32) u32 {
    s_mm_lock.lock();
    defer s_mm_lock.unlock();
    for (&s_mm) |*m| {
        if (!m.in_use or m.pid != pid) continue;
        var n: u32 = 0;
        for (m.anon) |r| {
            if (r.in_use) n += 1;
        }
        return n;
    }
    return 0;
}

fn sysMmap(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const length = a.arg[1];
    const prot = a.arg[2];
    const flags = a.arg[3];
    const fd: i64 = @bitCast(a.arg[4]);
    const addr = a.arg[0];

    if (length == 0 or length > 512 * 1024 * 1024) return -@as(i64, abi.EINVAL);
    if (flags & abi.MAP_ANONYMOUS == 0 and fd >= 0) return -@as(i64, abi.ENODEV);

    const pid = sched_if.current();
    if (pid == 0) return -@as(i64, abi.EINVAL);
    const root = userRootOf(pid);
    if (root == 0) return -@as(i64, abi.EINVAL);

    const pages = (length + PAGE_SIZE - 1) / PAGE_SIZE;

    s_mm_lock.lock();
    defer s_mm_lock.unlock();
    const st = mmStateLocked(pid) orelse return -@as(i64, abi.ENOMEM);

    var va: u64 = 0;
    if (flags & abi.MAP_FIXED != 0) {
        if (addr == 0 or addr & (PAGE_SIZE - 1) != 0) return -@as(i64, abi.EINVAL);
        va = addr;
    } else {
        va = st.mmap_top;
        if (va + pages * PAGE_SIZE > MMAP_LIMIT) return -@as(i64, abi.ENOMEM);
        st.mmap_top = (va + pages * PAGE_SIZE + 0xFFFF) & ~@as(u64, 0xFFFF);
    }

    const rec = for (&st.anon) |*r| {
        if (!r.in_use) break r;
    } else return -@as(i64, abi.ENOMEM);

    const order = orderForBytes(pages * PAGE_SIZE);
    var phys: u64 = 0;
    if (pmm_if.alloc_page(order, &phys) != 0) return -@as(i64, abi.ENOMEM);
    @memset(hhdm(phys)[0 .. pages * PAGE_SIZE], 0);

    if (mmu_if.map(root, va, phys, pages, .ps_4kb, protToMmuFlags(prot)) != 0) {
        _ = pmm_if.release(phys);
        return -@as(i64, abi.ENOMEM);
    }

    rec.* = .{ .in_use = true, .va = va, .phys = phys, .pages = pages };
    return @bitCast(va);
}

fn sysMunmap(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const addr = a.arg[0];
    const pid = sched_if.current();
    if (pid == 0) return 0;
    const root = userRootOf(pid);

    s_mm_lock.lock();
    defer s_mm_lock.unlock();
    const st = mmStateLocked(pid) orelse return 0;
    for (&st.anon) |*r| {
        if (r.in_use and r.va == addr) {
            if (root != 0) _ = mmu_if.unmap(root, r.va, r.pages, .ps_4kb);
            _ = pmm_if.release(r.phys);
            r.* = .{};
            return 0;
        }
    }
    return 0;
}

fn sysMprotect(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const addr = a.arg[0];
    const length = a.arg[1];
    const prot = a.arg[2];
    if (addr & (PAGE_SIZE - 1) != 0) return -@as(i64, abi.EINVAL);
    const pages = (length + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages == 0) return 0;
    const pid = sched_if.current();
    if (pid == 0) return -@as(i64, abi.EINVAL);
    const root = userRootOf(pid);
    if (root == 0) return -@as(i64, abi.EINVAL);
    if (mmu_if.protect(root, addr, pages, .ps_4kb, protToMmuFlags(prot)) != 0) return -@as(i64, abi.EINVAL);
    return 0;
}

fn sysMadvise(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = a;
    _ = ctx;
    return 0;
}

fn sysMremap(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = a;
    _ = ctx;
    return -@as(i64, abi.ENOSYS);
}

fn sysBrk(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const want = a.arg[0];
    const pid = sched_if.current();
    if (pid == 0) return @bitCast(BRK_BASE);
    const root = userRootOf(pid);

    s_mm_lock.lock();
    defer s_mm_lock.unlock();
    const st = mmStateLocked(pid) orelse return @bitCast(BRK_BASE);

    if (want == 0 or root == 0) return @bitCast(st.brk_cur);
    if (want < BRK_BASE or want > BRK_BASE + BRK_MAX) return @bitCast(st.brk_cur);

    const cur_pg = (st.brk_cur + PAGE_SIZE - 1) & ~@as(u64, PAGE_SIZE - 1);
    const want_pg = (want + PAGE_SIZE - 1) & ~@as(u64, PAGE_SIZE - 1);

    if (want_pg > cur_pg) {
        var v = cur_pg;
        while (v < want_pg) : (v += PAGE_SIZE) {
            const rec = for (&st.anon) |*r| {
                if (!r.in_use) break r;
            } else return @bitCast(st.brk_cur);
            var phys: u64 = 0;
            if (pmm_if.alloc_page(0, &phys) != 0) return @bitCast(st.brk_cur);
            @memset(hhdm(phys)[0..PAGE_SIZE], 0);
            if (mmu_if.map(root, v, phys, 1, .ps_4kb, abi.MMU_USER | abi.MMU_NO_EXEC) != 0) {
                _ = pmm_if.release(phys);
                return @bitCast(st.brk_cur);
            }
            rec.* = .{ .in_use = true, .va = v, .phys = phys, .pages = 1 };
        }
    }
    // Shrinking `brk` only lowers the logical break -- the pages stay
    // mapped and are reclaimed at process teardown (mmForget + mmu.free).
    // Calling mmu.unmap here corrupts pmm/mmu state in the full boot (an
    // unresolved interaction between pruneIfEmpty's table recycling and
    // the buddy allocator; see MUSL_SYSCALLS.md). mallocng uses mmap, not
    // brk, so this costs nothing in practice.
    st.brk_cur = want;
    return @bitCast(want);
}

// --- misc process / system syscalls musl's crt0 + coreutils touch -----
//
// Mostly stubs: enough that startup, malloc, and simple tools run. Real
// signal delivery, a sleep queue, and job control are later work.

var s_umask: u32 = 0o022;

fn cntNs() u64 {
    const cnt = asm volatile ("mrs %[v], cntvct_el0"
        : [v] "=r" (-> u64),
    );
    const frq = asm volatile ("mrs %[v], cntfrq_el0"
        : [v] "=r" (-> u64),
    );
    if (frq == 0) return 0;
    // ns = cnt * 1e9 / frq, done in two steps to limit overflow.
    const secs = cnt / frq;
    const rem = cnt % frq;
    return secs * 1_000_000_000 + (rem * 1_000_000_000) / frq;
}

fn cntVct() u64 {
    return asm volatile ("mrs %[v], cntvct_el0"
        : [v] "=r" (-> u64),
    );
}

fn cntFrq() u64 {
    return asm volatile ("mrs %[v], cntfrq_el0"
        : [v] "=r" (-> u64),
    );
}

/// Cooperative spin-sleep. No sleep queue yet: the task busy-waits (with
/// a `yield` CPU hint) until the deadline, bailing early with `EINTR`
/// (and writing the remainder) if a signal comes pending. IRQs stay
/// enabled -- a timer IRQ mid-spin is fine since the exceptions module
/// preserves SP_EL0 across a current-EL trap. Shared by `nanosleep`
/// (relative) and `clock_nanosleep` (relative + `TIMER_ABSTIME`); with
/// no wall clock, an absolute CLOCK_REALTIME request behaves like
/// CLOCK_MONOTONIC.
fn doSleep(req_ptr: u64, rem_ptr: u64, abs: bool) i64 {
    if (req_ptr == 0) return -@as(i64, abi.EFAULT);
    const req: [*]const i64 = @ptrFromInt(req_ptr);
    const sec = req[0];
    const nsec = req[1];
    if (sec < 0 or nsec < 0 or nsec >= 1_000_000_000) return -@as(i64, abi.EINVAL);

    const frq = cntFrq();
    if (frq == 0) return 0;
    const want_ns: u128 = @as(u128, @intCast(sec)) * 1_000_000_000 + @as(u128, @intCast(nsec));
    const want_ticks: u128 = want_ns * frq / 1_000_000_000;
    const deadline: u128 = if (abs) want_ticks else @as(u128, cntVct()) + want_ticks;

    const pid = sched_if.current();
    while (true) {
        asm volatile ("msr daifclr, #3" ::: .{ .memory = true }); // stay interruptible / tick
        const now: u128 = cntVct();
        if (now >= deadline) return 0;
        if (signal_if.has_pending(pid) != 0) {
            if (rem_ptr != 0) {
                const rem: [*]i64 = @ptrFromInt(rem_ptr);
                const left_ns: u128 = (deadline - now) * 1_000_000_000 / frq;
                rem[0] = @intCast(left_ns / 1_000_000_000);
                rem[1] = @intCast(left_ns % 1_000_000_000);
            }
            return -@as(i64, abi.EINTR);
        }
        asm volatile ("yield");
    }
}

fn sysNanosleep(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    return doSleep(a.arg[0], a.arg[1], false);
}

fn sysClockNanosleep(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    return doSleep(a.arg[2], a.arg[3], (a.arg[1] & 1) != 0); // TIMER_ABSTIME == 1
}

fn sysZero(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = a;
    _ = ctx;
    return 0;
}

fn sysEnosys(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = a;
    _ = ctx;
    return -@as(i64, abi.ENOSYS);
}

fn sysGettid(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = a;
    _ = ctx;
    return @intCast(sched_if.current());
}

fn sysSetpgid(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const pid: u32 = if (a.arg[0] == 0) sched_if.current() else @intCast(a.arg[0]);
    return process_if.set_pgid(pid, @intCast(a.arg[1]));
}

fn sysGetpgid(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const pid: u32 = if (a.arg[0] == 0) sched_if.current() else @intCast(a.arg[0]);
    const g = process_if.get_pgid(pid);
    return if (g == 0) -@as(i64, abi.ESRCH) else @intCast(g);
}

fn sysSetsid(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = a;
    _ = ctx;
    const s = process_if.set_sid(sched_if.current());
    return if (s == 0) -@as(i64, abi.EPERM) else @intCast(s);
}

fn sysGetsid(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const pid: u32 = if (a.arg[0] == 0) sched_if.current() else @intCast(a.arg[0]);
    const s = process_if.get_sid(pid);
    return if (s == 0) -@as(i64, abi.ESRCH) else @intCast(s);
}

fn zeroBytes(uptr: u64, n: usize) void {
    if (uptr == 0) return;
    const p: [*]u8 = @ptrFromInt(uptr);
    var i: usize = 0;
    while (i < n) : (i += 1) p[i] = 0;
}

fn sysGetrusage(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    zeroBytes(a.arg[1], 144); // struct rusage
    return 0;
}

fn sysGetcpu(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    zeroBytes(a.arg[0], 4); // *cpu = 0
    zeroBytes(a.arg[1], 4); // *node = 0
    return 0;
}

fn sysSchedGetparam(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    zeroBytes(a.arg[1], 4); // struct sched_param { int sched_priority; }
    return 0;
}

fn sysSetTidAddress(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = a;
    _ = ctx;
    return @intCast(sched_if.current()); // musl stores this as its tid
}

fn sysUname(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    if (a.arg[0] == 0) return -@as(i64, abi.EFAULT);
    const u: *abi.UtsName = @ptrFromInt(a.arg[0]);
    u.* = .{};
    const set = struct {
        fn f(dst: *[65]u8, s: []const u8) void {
            @memcpy(dst[0..s.len], s);
        }
    }.f;
    set(&u.sysname, "Linux");
    set(&u.nodename, "arm-os");
    set(&u.release, "6.1.0-arm-os");
    set(&u.version, "#1 ARM_OS");
    set(&u.machine, "aarch64");
    return 0;
}

fn sysGetrandom(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    if (a.arg[0] == 0) return 0;
    const buf: [*]u8 = @ptrFromInt(a.arg[0]);
    const n: usize = @intCast(a.arg[1]);
    fillRandom(buf[0..n]);
    return @intCast(n);
}

fn sysClockGettime(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    if (a.arg[1] == 0) return -@as(i64, abi.EFAULT);
    const ts: [*]i64 = @ptrFromInt(a.arg[1]);
    const ns = cntNs();
    ts[0] = @intCast(ns / 1_000_000_000);
    ts[1] = @intCast(ns % 1_000_000_000);
    return 0;
}

fn sysGettimeofday(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    if (a.arg[0] == 0) return 0;
    const tv: [*]i64 = @ptrFromInt(a.arg[0]);
    const ns = cntNs();
    tv[0] = @intCast(ns / 1_000_000_000);
    tv[1] = @intCast((ns % 1_000_000_000) / 1000); // usec
    return 0;
}

fn sysClockGetres(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    if (a.arg[1] != 0) {
        const ts: [*]i64 = @ptrFromInt(a.arg[1]);
        ts[0] = 0;
        ts[1] = 1;
    }
    return 0;
}

fn sysUmask(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const old = s_umask;
    s_umask = @truncate(a.arg[0] & 0o777);
    return @intCast(old);
}

fn sysSchedGetaffinity(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const len: usize = @intCast(a.arg[1]);
    if (a.arg[2] != 0 and len >= 8) {
        const p: [*]u8 = @ptrFromInt(a.arg[2]);
        p[0] = 1; // CPU 0 only
        var i: usize = 1;
        while (i < 8) : (i += 1) p[i] = 0;
    }
    return 8; // bytes of the mask written
}

fn sysSysinfo(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    if (a.arg[0] != 0) {
        const p: [*]u8 = @ptrFromInt(a.arg[0]);
        var i: usize = 0;
        while (i < 112) : (i += 1) p[i] = 0;
    }
    return 0;
}

fn sysPrlimit64(a: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    // arg2 = new limit (ignored), arg3 = old limit out
    if (a.arg[3] != 0) {
        const p: [*]u64 = @ptrFromInt(a.arg[3]);
        p[0] = 8 * 1024 * 1024; // rlim_cur
        p[1] = ~@as(u64, 0); // rlim_max = RLIM_INFINITY
    }
    return 0;
}


pub fn main(boot_info_ptr: *anyopaque) void {
    _ = boot_info_ptr;
    _ = mmu_if.get_user_ctx(&s_kernel_root); // kernel TTBR0 is still active here
    _ = sc_if.register(abi.SYS_getpid, &sysGetpid, null);
    _ = sc_if.register(abi.SYS_getppid, &sysGetppid, null);
    _ = sc_if.register(abi.SYS_sched_yield, &sysSchedYield, null);
    _ = sc_if.register(abi.SYS_exit, &sysExit, null);
    _ = sc_if.register(abi.SYS_exit_group, &sysExit, null);
    _ = sc_if.register(abi.SYS_wait4, &sysWait4, null);
    _ = sc_if.register_raw(abi.SYS_clone, &sysClone, null);
    _ = sc_if.register_raw(abi.SYS_execve, &sysExecve, null);
    _ = sc_if.register(abi.SYS_brk, &sysBrk, null);
    _ = sc_if.register(abi.SYS_mmap, &sysMmap, null);
    _ = sc_if.register(abi.SYS_munmap, &sysMunmap, null);
    _ = sc_if.register(abi.SYS_mprotect, &sysMprotect, null);
    _ = sc_if.register(abi.SYS_madvise, &sysMadvise, null);
    _ = sc_if.register(abi.SYS_mremap, &sysMremap, null);

    // identity / info -- single-user, uid 0
    _ = sc_if.register(abi.SYS_getuid, &sysZero, null);
    _ = sc_if.register(abi.SYS_geteuid, &sysZero, null);
    _ = sc_if.register(abi.SYS_getgid, &sysZero, null);
    _ = sc_if.register(abi.SYS_getegid, &sysZero, null);
    _ = sc_if.register(abi.SYS_gettid, &sysGettid, null);
    _ = sc_if.register(abi.SYS_set_tid_address, &sysSetTidAddress, null);
    _ = sc_if.register(abi.SYS_uname, &sysUname, null);
    _ = sc_if.register(abi.SYS_umask, &sysUmask, null);
    _ = sc_if.register(abi.SYS_getrandom, &sysGetrandom, null);
    _ = sc_if.register(abi.SYS_sysinfo, &sysSysinfo, null);
    _ = sc_if.register(abi.SYS_prlimit64, &sysPrlimit64, null);
    _ = sc_if.register(abi.SYS_sched_getaffinity, &sysSchedGetaffinity, null);
    _ = sc_if.register(abi.SYS_prctl, &sysZero, null);

    // time -- monotonic from the generic timer; no wall clock, no sleep queue
    _ = sc_if.register(abi.SYS_clock_gettime, &sysClockGettime, null);
    _ = sc_if.register(abi.SYS_gettimeofday, &sysGettimeofday, null);
    _ = sc_if.register(abi.SYS_clock_getres, &sysClockGetres, null);
    _ = sc_if.register(abi.SYS_nanosleep, &sysNanosleep, null);
    _ = sc_if.register(abi.SYS_clock_nanosleep, &sysClockNanosleep, null);

    // signals: rt_sig* / kill / delivery hook live in hnsorens.sys.signal
    _ = sc_if.register(abi.SYS_set_robust_list, &sysZero, null);

    // session / process groups (real, via the process module)
    _ = sc_if.register(abi.SYS_setpgid, &sysSetpgid, null);
    _ = sc_if.register(abi.SYS_getpgid, &sysGetpgid, null);
    _ = sc_if.register(abi.SYS_setsid, &sysSetsid, null);
    _ = sc_if.register(abi.SYS_getsid, &sysGetsid, null);

    // threads -- single-thread, no real futex
    _ = sc_if.register(abi.SYS_futex, &sysZero, null);
    // ppoll -> real, registered by the fd module

    // Tier C -- accepted / canned; safe because "success, no state" is
    // the correct behaviour for each of these on a single-CPU box.
    _ = sc_if.register(abi.SYS_getgroups, &sysZero, null); // no supplementary groups
    _ = sc_if.register(abi.SYS_getrusage, &sysGetrusage, null);
    _ = sc_if.register(abi.SYS_times, &sysZero, null);
    _ = sc_if.register(abi.SYS_getpriority, &sysZero, null);
    _ = sc_if.register(abi.SYS_setpriority, &sysZero, null);
    _ = sc_if.register(abi.SYS_sched_setscheduler, &sysZero, null);
    _ = sc_if.register(abi.SYS_sched_getscheduler, &sysZero, null); // SCHED_OTHER
    _ = sc_if.register(abi.SYS_sched_getparam, &sysSchedGetparam, null);
    _ = sc_if.register(abi.SYS_sched_get_priority_max, &sysZero, null);
    _ = sc_if.register(abi.SYS_sched_get_priority_min, &sysZero, null);
    _ = sc_if.register(abi.SYS_personality, &sysZero, null);
    _ = sc_if.register(abi.SYS_membarrier, &sysZero, null);
    _ = sc_if.register(abi.SYS_getcpu, &sysGetcpu, null);
    _ = sc_if.register(abi.SYS_fadvise64, &sysZero, null);

    kernel_fmt.print(serial_if, "[elf_loader] process + tree + anon mmap/brk + misc syscalls\n", .{});
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
