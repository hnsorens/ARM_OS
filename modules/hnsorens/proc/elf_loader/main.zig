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
pub const sched_if = abi.importInterface(abi.Scheduler);
pub const sc_if = abi.importInterface(abi.Syscalls);
pub const serial_if = abi.importInterface(abi.Serial);

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

    freeImageParts(img.uroot, img.segs[0..img.n_segs], img.stack_phys);
    img.* = .{};
    return 0;
}

// --- process syscalls -------------------------------------------

fn sysWrite(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const fd = args.arg[0];
    const buf = args.arg[1];
    const len = args.arg[2];
    if (fd != 1 and fd != 2) return -@as(i64, abi.EBADF);
    if (len == 0) return 0;
    // The calling process's TTBR0 is still active here, so `buf` (a user
    // VA) is dereferenceable; the identity-map copy keeps the UART MMIO
    // reachable for serial_if.write.
    _ = serial_if.write(@as([*]const u8, @ptrFromInt(buf)), len);
    return @intCast(len);
}

fn sysGetpid(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = args;
    _ = ctx;
    return @intCast(sched_if.current());
}

fn sysExit(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const pid = sched_if.current();
    if (pid != 0) {
        const code: i32 = @truncate(@as(i64, @bitCast(args.arg[0])));
        _ = process_if.set_exit_code(pid, code);
    }
    sched_if.exit_current();
    return 0; // not reached
}

fn sysSchedYield(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = args;
    _ = ctx;
    sched_if.yield();
    return 0;
}

pub fn main(boot_info_ptr: *anyopaque) void {
    _ = boot_info_ptr;
    _ = sc_if.register(abi.SYS_write, &sysWrite, null);
    _ = sc_if.register(abi.SYS_getpid, &sysGetpid, null);
    _ = sc_if.register(abi.SYS_exit, &sysExit, null);
    _ = sc_if.register(abi.SYS_exit_group, &sysExit, null);
    _ = sc_if.register(abi.SYS_sched_yield, &sysSchedYield, null);
    kernel_fmt.print(serial_if, "[elf_loader] ready; process syscalls registered\n", .{});
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
