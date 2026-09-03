//! Task control blocks + kernel-thread lifecycle, exporting `Process`
//! (category "process"). A fixed table of `MAX_PROCESSES` TCBs; each live
//! one embeds a `TaskContext` (built via the context_switch module, so
//! the scheduler can `switch_to` it) and owns a power-of-two page block
//! from `pmm` used as its kernel stack, reached through the HHDM alias.
//!
//! Scope for now: kernel threads only, all sharing the kernel address
//! space. User address spaces (`address_space`) and process loading come
//! with the ELF-loader module. This module has no notion of "the current
//! process" or a ready queue -- that's the scheduler's job; this is just
//! TCB storage + lifecycle.
//!
//! HendOS had a monolithic `process_t` (fds, cwd, groups, sessions,
//! signals, heap pointers, ...); this deliberately keeps only the
//! scheduling essentials and lets later modules layer the rest on via the
//! pid.
const abi = @import("abi");
const kernel_fmt = @import("kernel_fmt");
const spinlock = @import("spinlock");

pub const pmm_if = abi.importInterface(abi.Pmm);
pub const cs_if = abi.importInterface(abi.ContextSwitch);
pub const serial_if = abi.importInterface(abi.Serial);

pub const MAX_PROCESSES = 64;
pub const KSTACK_MAX_PAGES = 64; // 256 KiB
const PAGE_SIZE: u64 = 4096;

const Tcb = struct {
    in_use: bool = false,
    is_user: bool = false,
    pid: u32 = 0,
    parent: u32 = 0,
    state: abi.ProcessState = .dead,
    priority: u32 = 0,
    exit_code: i32 = 0,
    address_space: u64 = 0,
    kstack_phys: u64 = 0,
    kstack_base: u64 = 0,
    kstack_size: u64 = 0,
    context: abi.TaskContext = .{},
    name: [32]u8 = [_]u8{0} ** 32,
};

var s_table: [MAX_PROCESSES]Tcb = [_]Tcb{.{}} ** MAX_PROCESSES;
var s_next_pid: u32 = 1;
var s_lock: spinlock.SpinLock = .{};

fn slotOf(pid: u32) ?*Tcb {
    for (&s_table) |*t| {
        if (t.in_use and t.pid == pid) return t;
    }
    return null;
}

fn orderForPages(pages: u32) u8 {
    var order: u8 = 0;
    while ((@as(u64, 1) << @as(u6, @intCast(order))) < pages) order += 1;
    return order;
}

// --- exported vtable --------------------------------------------------

pub fn createKernelThread(name: [*:0]const u8, entry: usize, arg: usize, kstack_pages: u32, priority: u32, out_pid: *u32) callconv(.c) c_int {
    if (entry == 0 or kstack_pages == 0 or kstack_pages > KSTACK_MAX_PAGES) return abi.EINVAL;

    s_lock.lock();
    defer s_lock.unlock();

    const slot = blk: {
        for (&s_table) |*t| {
            if (!t.in_use) break :blk t;
        }
        return abi.ENOMEM;
    };

    const order = orderForPages(kstack_pages);
    var phys: u64 = undefined;
    const st = pmm_if.alloc_page(order, &phys);
    if (st != 0) return st;

    const size = (@as(u64, 1) << @as(u6, @intCast(order))) * PAGE_SIZE;
    const base = phys + abi.HHDM_OFFSET;
    const top = base + size;

    slot.* = .{
        .in_use = true,
        .pid = s_next_pid,
        .state = .ready,
        .priority = priority,
        .kstack_phys = phys,
        .kstack_base = base,
        .kstack_size = size,
    };
    if (cs_if.init_kernel_context(&slot.context, entry, arg, top) != 0) {
        _ = pmm_if.release(phys);
        slot.* = .{};
        return abi.EINVAL;
    }

    copyName(&slot.name, name);
    out_pid.* = s_next_pid;
    s_next_pid += 1;
    return 0;
}

pub fn createUserProcess(name: [*:0]const u8, ttbr0: u64, user_entry: u64, user_sp: u64, kstack_pages: u32, priority: u32, out_pid: *u32) callconv(.c) c_int {
    if (ttbr0 == 0 or user_entry == 0 or kstack_pages == 0 or kstack_pages > KSTACK_MAX_PAGES) return abi.EINVAL;

    s_lock.lock();
    defer s_lock.unlock();

    const slot = blk: {
        for (&s_table) |*t| {
            if (!t.in_use) break :blk t;
        }
        return abi.ENOMEM;
    };

    const order = orderForPages(kstack_pages);
    var phys: u64 = undefined;
    const st = pmm_if.alloc_page(order, &phys);
    if (st != 0) return st;

    const size = (@as(u64, 1) << @as(u6, @intCast(order))) * PAGE_SIZE;
    const base = phys + abi.HHDM_OFFSET;
    const top = base + size;

    slot.* = .{
        .in_use = true,
        .is_user = true,
        .pid = s_next_pid,
        .state = .ready,
        .priority = priority,
        .address_space = ttbr0,
        .kstack_phys = phys,
        .kstack_base = base,
        .kstack_size = size,
    };
    if (cs_if.init_user_context(&slot.context, top, ttbr0, user_entry, user_sp) != 0) {
        _ = pmm_if.release(phys);
        slot.* = .{};
        return abi.EINVAL;
    }

    copyName(&slot.name, name);
    out_pid.* = s_next_pid;
    s_next_pid += 1;
    return 0;
}

pub fn createForkedProcess(name: [*:0]const u8, ttbr0: u64, parent: u32, frame: *const abi.TrapFrame, kstack_pages: u32, priority: u32, out_pid: *u32) callconv(.c) c_int {
    if (ttbr0 == 0 or kstack_pages == 0 or kstack_pages > KSTACK_MAX_PAGES) return abi.EINVAL;

    s_lock.lock();
    defer s_lock.unlock();

    const slot = blk: {
        for (&s_table) |*t| {
            if (!t.in_use) break :blk t;
        }
        return abi.ENOMEM;
    };

    const order = orderForPages(kstack_pages);
    var phys: u64 = undefined;
    const st = pmm_if.alloc_page(order, &phys);
    if (st != 0) return st;

    const size = (@as(u64, 1) << @as(u6, @intCast(order))) * PAGE_SIZE;
    const base = phys + abi.HHDM_OFFSET;
    const top = base + size;

    slot.* = .{
        .in_use = true,
        .is_user = true,
        .pid = s_next_pid,
        .parent = parent,
        .state = .ready,
        .priority = priority,
        .address_space = ttbr0,
        .kstack_phys = phys,
        .kstack_base = base,
        .kstack_size = size,
    };
    if (cs_if.init_forked_context(&slot.context, top, ttbr0, frame) != 0) {
        _ = pmm_if.release(phys);
        slot.* = .{};
        return abi.EINVAL;
    }

    copyName(&slot.name, name);
    out_pid.* = s_next_pid;
    s_next_pid += 1;
    return 0;
}

pub fn setAddressSpace(pid: u32, ttbr0: u64) callconv(.c) c_int {
    s_lock.lock();
    defer s_lock.unlock();
    const slot = slotOf(pid) orelse return abi.EINVAL;
    slot.address_space = ttbr0;
    slot.context.ttbr0 = ttbr0;
    return 0;
}

pub fn setParent(pid: u32, parent: u32) callconv(.c) c_int {
    s_lock.lock();
    defer s_lock.unlock();
    const slot = slotOf(pid) orelse return abi.EINVAL;
    slot.parent = parent;
    return 0;
}

fn copyName(dst: *[32]u8, src: [*:0]const u8) void {
    var i: usize = 0;
    while (i < dst.len - 1 and src[i] != 0) : (i += 1) dst[i] = src[i];
}

pub fn destroy(pid: u32) callconv(.c) c_int {
    s_lock.lock();
    defer s_lock.unlock();

    const slot = slotOf(pid) orelse return abi.EINVAL;
    if (slot.state == .running) return abi.EBUSY;

    _ = pmm_if.release(slot.kstack_phys);
    slot.* = .{};
    return 0;
}

pub fn exists(pid: u32) callconv(.c) bool {
    s_lock.lock();
    defer s_lock.unlock();
    return slotOf(pid) != null;
}

pub fn contextOf(pid: u32, out: *?*anyopaque) callconv(.c) c_int {
    s_lock.lock();
    defer s_lock.unlock();
    const slot = slotOf(pid) orelse {
        out.* = null;
        return abi.EINVAL;
    };
    out.* = @ptrCast(&slot.context);
    return 0;
}

pub fn getInfo(pid: u32, out: *abi.ProcessInfo) callconv(.c) c_int {
    s_lock.lock();
    defer s_lock.unlock();
    const slot = slotOf(pid) orelse return abi.EINVAL;
    out.* = .{
        .pid = slot.pid,
        .parent = slot.parent,
        .state = slot.state,
        .priority = slot.priority,
        .exit_code = slot.exit_code,
        .address_space = slot.address_space,
        .kstack_base = slot.kstack_base,
        .kstack_size = slot.kstack_size,
        .is_user = slot.is_user,
    };
    @memcpy(&out.name, &slot.name);
    return 0;
}

pub fn getState(pid: u32, out: *abi.ProcessState) callconv(.c) c_int {
    s_lock.lock();
    defer s_lock.unlock();
    const slot = slotOf(pid) orelse return abi.EINVAL;
    out.* = slot.state;
    return 0;
}

pub fn setState(pid: u32, state: abi.ProcessState) callconv(.c) c_int {
    const v = @intFromEnum(state);
    if (v == @intFromEnum(abi.ProcessState.dead) or v > @intFromEnum(abi.ProcessState.zombie)) return abi.EINVAL;

    s_lock.lock();
    defer s_lock.unlock();
    const slot = slotOf(pid) orelse return abi.EINVAL;
    slot.state = state;
    return 0;
}

pub fn getPriority(pid: u32, out: *u32) callconv(.c) c_int {
    s_lock.lock();
    defer s_lock.unlock();
    const slot = slotOf(pid) orelse return abi.EINVAL;
    out.* = slot.priority;
    return 0;
}

pub fn setPriority(pid: u32, priority: u32) callconv(.c) c_int {
    s_lock.lock();
    defer s_lock.unlock();
    const slot = slotOf(pid) orelse return abi.EINVAL;
    slot.priority = priority;
    return 0;
}

pub fn setExitCode(pid: u32, code: i32) callconv(.c) c_int {
    s_lock.lock();
    defer s_lock.unlock();
    const slot = slotOf(pid) orelse return abi.EINVAL;
    slot.exit_code = code;
    return 0;
}

pub fn count() callconv(.c) u32 {
    s_lock.lock();
    defer s_lock.unlock();
    var n: u32 = 0;
    for (s_table) |t| {
        if (t.in_use) n += 1;
    }
    return n;
}

pub fn list(out_pids: [*]u32, max: u32, n_out: *u32) callconv(.c) c_int {
    s_lock.lock();
    defer s_lock.unlock();
    var n: u32 = 0;
    var overflow = false;
    for (s_table) |t| {
        if (!t.in_use) continue;
        if (n < max) {
            out_pids[n] = t.pid;
            n += 1;
        } else {
            overflow = true;
        }
    }
    n_out.* = n;
    return if (overflow) abi.EOVERFLOW else 0;
}

pub fn main(boot_info_ptr: *anyopaque) void {
    _ = boot_info_ptr;
    kernel_fmt.print(serial_if, "[process] TCB table ready ({d} slots)\n", .{MAX_PROCESSES});
}

comptime {
    abi.exportInterface("table", abi.Process, .{
        .create_kernel_thread = createKernelThread,
        .create_user_process = createUserProcess,
        .create_forked_process = createForkedProcess,
        .set_address_space = setAddressSpace,
        .set_parent = setParent,
        .destroy = destroy,
        .exists = exists,
        .context_of = contextOf,
        .get_info = getInfo,
        .get_state = getState,
        .set_state = setState,
        .get_priority = getPriority,
        .set_priority = setPriority,
        .set_exit_code = setExitCode,
        .count = count,
        .list = list,
    });
}

comptime {
    _ = @import("test.zig");
}
