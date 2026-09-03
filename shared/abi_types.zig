//! Shared ABI types used by both the bootloader and kernel modules.
//!
//! This file must contain NO side-effectful exports (no `_start`, no link
//! sections), so the bootloader can import it freely. Module-only entry/export
//! machinery lives in `abi.zig`, which re-exports everything from here.

pub const PAGE_SIZE: u64 = 4096;

/// Virtual base of the region where modules are loaded (upper half, TTBR1).
pub const VIRTUAL_MODULE_LOAD_START: u64 = 0xFFFFFF8000000000;

/// Offset applied to a physical address to reach its HHDM direct-map alias.
pub const HHDM_OFFSET: u64 = 0xFFFF800000000000;

/// Boot stack size in pages, mapped at VIRTUAL_MODULE_LOAD_START.
pub const MODULE_STACK_SIZE_PAGES: u64 = 0x100;

/// The first module is loaded this far past VIRTUAL_MODULE_LOAD_START so it
/// does not collide with the boot stack (the C bootloader had this bug).
pub const INITIAL_LOAD_OFFSET: u64 = MODULE_STACK_SIZE_PAGES * PAGE_SIZE;

// --- Test result codes (must match the C TEST_ macros) ---
pub const TEST_PASS: i32 = 0;
pub const TEST_FAIL: i32 = 1;
pub const TEST_SKIP: i32 = 2;

// --- errno values used by module vtables (match the kernel errno.h) ---
pub const EINVAL: c_int = 22;
pub const ENOSYS: c_int = 38;
pub const ENOEXEC: c_int = 8;
pub const EBADF: c_int = 9;
pub const ECHILD: c_int = 10;
pub const ESPIPE: c_int = 29;
pub const EMFILE: c_int = 24;
pub const ENFILE: c_int = 23;
pub const ENOMEM: c_int = 12;
pub const EFAULT: c_int = 14;
pub const EBUSY: c_int = 16;
pub const EEXIST: c_int = 17;
pub const EOVERFLOW: c_int = 139;
pub const EIO: c_int = 5;
pub const ENOENT: c_int = 2;
pub const ENOTDIR: c_int = 20;
pub const EISDIR: c_int = 21;
pub const ENOSPC: c_int = 28;
pub const ENOTEMPTY: c_int = 39;
pub const ELOOP: c_int = 40;
pub const ENAMETOOLONG: c_int = 36;
pub const EPERM: c_int = 1;
pub const EINTR: c_int = 4;
pub const ENXIO: c_int = 6;
pub const E2BIG: c_int = 7;
pub const EAGAIN: c_int = 11;
pub const EACCES: c_int = 13;
pub const ENODEV: c_int = 19;
pub const ENOTTY: c_int = 25;
pub const ERANGE: c_int = 34;
pub const EPIPE: c_int = 32;
pub const ESRCH: c_int = 3;
pub const EDESTADDRREQ: c_int = 89;

// --- Boot info passed to every module entry ---
pub const MemoryType = enum(u32) {
    unknown = 0,
    free = 1,
    used = 2,
};

pub const MemoryRegion = extern struct {
    start: u64,
    /// Number of 4 KiB pages in this region.
    page_count: u64,
    memory_type: MemoryType,
};

pub const BootInfo = extern struct {
    memory_regions: [*]MemoryRegion,
    memory_map_size: u64,
    virtual_start: u64,
};

// --- Vtable types. Signatures match the C include/api/*.h headers. ---

// NOTE: the C reference driver exposes this as a variadic `printf(fmt, ...)`.
// This toolchain's C-varargs support (`@cVaStart`) is disabled ("disabled
// due to miscompilations" in std.builtin), so a real variadic function body
// can't be written at all here. Every caller in this codebase is a Zig
// module anyway, so there's no ABI reason to keep C varargs: modules format
// with `std.fmt` at the call site (see `shared/kernel_fmt.zig`) and this
// interface only needs to move already-formatted bytes.
pub const Serial = extern struct {
    write: *const fn (ptr: [*]const u8, len: usize) callconv(.c) usize,
};

pub const Pmm = extern struct {
    alloc_page: *const fn (page_order: u8, out_frame: *u64) callconv(.c) c_int,
    alloc_aligned: *const fn (count: u64, alignment: u64, out: *u64) callconv(.c) c_int,
    alloc_in_range: *const fn (count: u64, max_addr: u64, out: *u64) callconv(.c) c_int,
    retain: *const fn (frame: u64) callconv(.c) c_int,
    release: *const fn (frame: u64) callconv(.c) c_int,
    get_total_memory: *const fn () callconv(.c) u64,
    get_free_memory: *const fn () callconv(.c) u64,
    reserve_range: *const fn (start: u64, sz: u64) callconv(.c) c_int,
};

pub const PageSize = enum(u32) {
    ps_4kb = 0x1000,
    ps_2mb = 0x200000,
    ps_1gb = 0x40000000,
};

// MMU flag bitmask constants (architectural Stage 1 descriptor fields).
pub const MMU_RO: u64 = 1 << 7;
pub const MMU_USER: u64 = 1 << 6;
pub const MMU_NO_EXEC: u64 = 1 << 54;
pub const MMU_NOCACHE: u64 = 1 << 2;
pub const MMU_WRITE_THROUGH: u64 = 2 << 2;

pub const Mmu = extern struct {
    alloc: *const fn (out_root: *u64) callconv(.c) c_int,
    free: *const fn (root: u64) callconv(.c) c_int,
    copy: *const fn (src_root: u64, dest_root: *u64) callconv(.c) c_int,
    /// Deep copy for `fork(2)`: like `copy` but every leaf page is a fresh
    /// physical frame with contents duplicated (independent memory), while
    /// the shared kernel-identity 1 GiB blocks stay shared. Pair with
    /// `free_all`.
    fork: *const fn (src_root: u64, dest_root: *u64) callconv(.c) c_int,
    /// Teardown for a `fork`-created space: frees the table frames AND
    /// every leaf frame (which `free` leaves to the caller), still
    /// skipping the shared identity blocks.
    free_all: *const fn (root: u64) callconv(.c) c_int,
    set_user_ctx: *const fn (root: u64, asid: u16) callconv(.c) c_int,
    set_kernel_ctx: *const fn (root: u64, asid: u16) callconv(.c) c_int,
    get_user_ctx: *const fn (root: *u64) callconv(.c) c_int,
    get_kernel_ctx: *const fn (root: *u64) callconv(.c) c_int,
    map: *const fn (root: u64, virt: u64, phys: u64, pg_count: u64, pg_size: PageSize, flags: u64) callconv(.c) c_int,
    unmap: *const fn (root: u64, virt: u64, pg_count: u64, pg_size: PageSize) callconv(.c) c_int,
    protect: *const fn (root: u64, virt: u64, pg_count: u64, pg_size: PageSize, flags: u64) callconv(.c) c_int,
    translate: *const fn (root: u64, virt: u64, phys_out: *u64, flags_out: *u64) callconv(.c) c_int,
    flush: *const fn () callconv(.c) c_int,
    invalidate: *const fn (virt: u64, pg_count: u64, pg_size: PageSize) callconv(.c) c_int,
    set_mair: *const fn (mair: u64) callconv(.c) c_int,
};

pub const RegionType = enum(u32) {
    free = 0,
    code = 1,
    data = 2,
    stack = 3,
    heap = 4,
    mmio = 5,
    guard = 6,
};

pub const VmmRegionInfo = extern struct {
    base: u64,
    size: u64,
    flags: u64,
    region_type: RegionType,
    is_paged: bool,
};

pub const Vmm = extern struct {
    space_create: *const fn (out_table_root: *u64) callconv(.c) c_int,
    space_destroy: *const fn (table_root: u64) callconv(.c) c_int,
    allocate: *const fn (root: u64, vaddr: *u64, sz: u64, flags: u64, region_type: RegionType) callconv(.c) c_int,
    reserve: *const fn (root: u64, vaddr: u64, sz: u64) callconv(.c) c_int,
    free: *const fn (root: u64, vaddr: u64, sz: u64) callconv(.c) c_int,
    resize: *const fn (root: u64, vaddr: u64, old_sz: u64, new_sz: u64) callconv(.c) c_int,
    map_external: *const fn (root: u64, v: u64, p: u64, sz: u64, f: u64) callconv(.c) c_int,
    protect: *const fn (root: u64, vaddr: u64, sz: u64, new_flags: u64) callconv(.c) c_int,
    query: *const fn (root: u64, vaddr: u64, out_info: *VmmRegionInfo) callconv(.c) c_int,
    activate: *const fn (root: u64) callconv(.c) c_int,
    sync: *const fn (root: u64, vaddr: u64, sz: u64) callconv(.c) c_int,
};

pub const Heap = extern struct {
    create: *const fn (root: u64, sz: u64, out_heap: *?*anyopaque) callconv(.c) c_int,
    destroy: *const fn (heap: ?*anyopaque) callconv(.c) c_int,
    malloc: *const fn (heap: ?*anyopaque, size: u64, out_ptr: *?*anyopaque) callconv(.c) c_int,
    free: *const fn (heap: ?*anyopaque, ptr: ?*anyopaque) callconv(.c) c_int,
    realloc: *const fn (heap: ?*anyopaque, ptr: ?*anyopaque, new_size: u64, out_ptr: *?*anyopaque) callconv(.c) c_int,
    memalign: *const fn (heap: ?*anyopaque, alignment: u64, size: u64, out_ptr: *?*anyopaque) callconv(.c) c_int,
    get_stats: *const fn (heap: ?*anyopaque, used: *u64, total: *u64) callconv(.c) c_int,
};

pub const Slab = extern struct {
    create_cache: *const fn (root: u64, obj_size: u64, alignment: u64, out_cache: *?*anyopaque) callconv(.c) c_int,
    destroy_cache: *const fn (cache: ?*anyopaque) callconv(.c) c_int,
    alloc: *const fn (cache: ?*anyopaque, out_obj: *?*anyopaque) callconv(.c) c_int,
    free: *const fn (cache: ?*anyopaque, obj: ?*anyopaque) callconv(.c) c_int,
    shrink: *const fn (cache: ?*anyopaque) callconv(.c) c_int,
};

pub const IrqTrigger = enum(u32) {
    level = 0,
    edge = 1,
};

pub const IrqGroup = enum(u32) {
    secure = 0,
    non_secure = 1,
};

pub const IsrHandler = *const fn (context: ?*anyopaque) callconv(.c) void;

pub const InterruptManager = extern struct {
    init_global: *const fn (d_base: u64, r_base: u64) callconv(.c) c_int,
    init_core: *const fn () callconv(.c) c_int,
    set_core_priority_mask: *const fn (mask: u32) callconv(.c) c_int,
    enable: *const fn (vector: u32) callconv(.c) c_int,
    disable: *const fn (vector: u32) callconv(.c) c_int,
    configure: *const fn (vector: u32, trigger: IrqTrigger, priority: u32) callconv(.c) c_int,
    set_group: *const fn (vector: u32, group: IrqGroup) callconv(.c) c_int,
    route_to_core: *const fn (vector: u32, mpidr_or_apicid: u64) callconv(.c) c_int,
    acknowledge: *const fn () callconv(.c) u32,
    end_of_interrupt: *const fn (vector: u32) callconv(.c) c_int,
    register_handler: *const fn (vector: u32, handler: IsrHandler, arg: ?*anyopaque) callconv(.c) c_int,
    unregister_handler: *const fn (vector: u32) callconv(.c) c_int,
};

pub const TimerCallback = *const fn (id: u32, context: ?*anyopaque) callconv(.c) void;

pub const Timer = extern struct {
    register_callback: *const fn (ticks_period: u32, periodic: u8, callback: TimerCallback, id: *u32, context: ?*anyopaque) callconv(.c) c_int,
    unregister_callback: *const fn (id: u32) callconv(.c) c_int,
    pause: *const fn (id: u32) callconv(.c) c_int,
    unpause: *const fn (id: u32) callconv(.c) c_int,
    modify: *const fn (id: u32, new_period: u32) callconv(.c) c_int,
    get_system_ticks: *const fn (ticks: *u64) callconv(.c) c_int,
    delay_ticks: *const fn (ticks: u32) callconv(.c) c_int,
};

// --- Cooperative CPU context switching (AArch64 EL1) ---
//
// `hnsorens.sched.context_switch` exports this. A `TaskContext` holds the
// register state the AArch64 C ABI requires a function call to preserve --
// the callee-saved GPRs x19-x28, the frame pointer (x29), the link
// register (x30), the stack pointer -- plus the per-task CPU state a
// switch must not blur between tasks: TTBR0_EL1 (address space), TPIDR_EL0
// (userspace thread pointer / TLS) and the full FP/SIMD register file
// (q0-q31, FPSR, FPCR). Saving/restoring that set (and swapping SP) turns
// a plain function call into a coroutine switch.
//
// Caller-saved GPRs (x0-x18) and NZCV are deliberately NOT saved: a
// cooperative `switch_to` happens at a call boundary where the compiler
// already treats those as clobbered. FP/SIMD *is* saved despite being
// mostly caller-saved, because userspace (musl) runs NEON between switch
// points and the lower 64 bits of v8-v15 are callee-saved. A preemptive
// switch (out of an exception handler) saves the full trap frame
// separately in the vector trampoline and only reuses this for the
// SP/callee-saved half.
pub const TaskContext = extern struct {
    x19: u64 = 0,
    x20: u64 = 0,
    x21: u64 = 0,
    x22: u64 = 0,
    x23: u64 = 0,
    x24: u64 = 0,
    x25: u64 = 0,
    x26: u64 = 0,
    x27: u64 = 0,
    x28: u64 = 0,
    /// Frame pointer (x29).
    fp: u64 = 0,
    /// Link register (x30) -- the address `switch_to`/`jump_to` resume at.
    lr: u64 = 0,
    /// Stack pointer to install (SP_EL1 for kernel tasks).
    sp: u64 = 0,
    /// Value to load into TTBR0_EL1 on switch -- already composed as
    /// `(asid << 48) | page_table_root`. `init_kernel_context` snapshots
    /// the current (kernel identity) TTBR0 here so every kernel task
    /// carries it explicitly; `init_user_context` sets a user address
    /// space. `switch_to` saves and restores it around every switch, so a
    /// user process's page tables can be freed safely once no task holds
    /// its root.
    ttbr0: u64 = 0, // offset 104

    /// TPIDR_EL0 -- the userspace thread pointer (musl/glibc TLS base;
    /// `errno` lives off it). Userspace sets it itself via `msr`; the
    /// kernel only has to preserve it per task. `switch_to` saves and
    /// restores it so two user processes don't share one TLS block.
    tpidr: u64 = 0, // offset 112

    /// FPSR / FPCR at the switch point. FPCR = 0 is the ABI-default
    /// userspace entry state (round-to-nearest, no traps), which the
    /// zeroed context gives a freshly `init_user_context`'d task.
    fpsr: u64 = 0, // offset 120
    fpcr: u64 = 0, // offset 128

    /// FP/SIMD register file q0..q31 (32 * 128 bits = 512 bytes), saved
    /// and restored by `switch_to`. musl's `memcpy`/`memset`/`strlen` are
    /// NEON, so this is not optional once anything libc-linked runs.
    /// `align(16)` so the `stp q`/`ldp q` pairs in the switch asm are
    /// well-formed regardless of where the struct lands.
    v: [64]u64 align(16) = [_]u64{0} ** 64, // offset 144, ends 656
};

/// Raw AArch64 register-file switching, exported by the context_switch
/// module (category "contextswitch"). Address-space (TTBR0) switching is
/// intentionally left to `Mmu.set_user_ctx` -- this module only moves the
/// CPU register state.
pub const ContextSwitch = extern struct {
    /// Initializes `ctx` so the first switch into it begins executing
    /// `entry(arg)` on the stack ending at `stack_top` (grows down;
    /// rounded down to a 16-byte boundary). `EINVAL` for a null `entry`
    /// or an implausibly small `stack_top`. If `entry` ever returns, the
    /// task is parked (logged, then a WFI loop) -- a scheduler is meant to
    /// hand tasks a real exit path instead.
    init_kernel_context: *const fn (ctx: *TaskContext, entry: usize, arg: usize, stack_top: u64) callconv(.c) c_int,
    /// Initializes `ctx` so the first switch into it runs on the kernel
    /// stack ending at `kstack_top`, installs `ttbr0` (already composed as
    /// `(asid << 48) | root`) as the address space, and immediately drops
    /// to EL0 at `user_entry` with SP_EL0 = `user_sp` and a cleared
    /// PSTATE (EL0t, interrupts unmasked). `EINVAL` for a null
    /// `user_entry` / `ttbr0`, or an implausibly small `kstack_top`.
    init_user_context: *const fn (ctx: *TaskContext, kstack_top: u64, ttbr0: u64, user_entry: u64, user_sp: u64) callconv(.c) c_int,
    /// Initializes `ctx` for a forked child: copies `frame` (the parent's
    /// trap frame at the fork `svc`) onto the child's kernel stack, forces
    /// its x0 to 0, installs `ttbr0`, and arranges the first switch into
    /// `ctx` to `eret` to EL0 with that (copied) register + PC + SP_EL0
    /// state -- so the child returns 0 from `fork()` right where the
    /// parent called it.
    init_forked_context: *const fn (ctx: *TaskContext, kstack_top: u64, ttbr0: u64, frame: *const TrapFrame) callconv(.c) c_int,
    /// Saves the current execution context into `save`, then resumes
    /// `restore`. Returns (in `save`'s context) only once some later
    /// switch targets `save`.
    switch_to: *const fn (save: *TaskContext, restore: *const TaskContext) callconv(.c) void,
    /// Like `switch_to` but with no context to save -- for the scheduler's
    /// first-ever entry into a task, where no prior context needs to be
    /// resumable. Never returns.
    jump_to: *const fn (restore: *const TaskContext) callconv(.c) noreturn,
};

// --- Processes / kernel threads ---
//
// `hnsorens.proc.process` owns a fixed table of task control blocks. Each
// TCB embeds a `TaskContext` (the scheduler switches to it via
// `ContextSwitch.switch_to`) and a kernel stack carved from `pmm`,
// addressed through its HHDM alias so it is always mapped. User address
// spaces are not wired here yet -- kernel threads share the kernel
// address space (`address_space == 0`).

/// Non-exhaustive so a stray value arriving across the ABI is inspectable
/// rather than illegal behavior.
pub const ProcessState = enum(u32) {
    /// TCB slot is free.
    dead = 0,
    /// Created, context initialized, not yet runnable.
    new = 1,
    /// Runnable; waiting for the scheduler to pick it.
    ready = 2,
    /// Currently executing on a core.
    running = 3,
    /// Waiting on something; not runnable until woken.
    blocked = 4,
    /// Exited; TCB kept until reaped for its exit code.
    zombie = 5,
    _,
};

pub const ProcessInfo = extern struct {
    pid: u32 = 0,
    /// Parent pid (0 for the first process / after reparenting to a gone
    /// parent).
    parent: u32 = 0,
    state: ProcessState = .dead,
    priority: u32 = 0,
    exit_code: i32 = 0,
    /// TTBR0 root for a user process; 0 for a kernel thread.
    address_space: u64 = 0,
    /// HHDM virtual address of the kernel stack's low end.
    kstack_base: u64 = 0,
    /// Kernel stack size in bytes (power-of-two page block actually
    /// allocated, which may exceed the requested page count).
    kstack_size: u64 = 0,
    /// True for a process created with `create_user_process` (runs at EL0
    /// in its own address space); false for a kernel thread.
    is_user: bool = false,
    name: [32]u8 = [_]u8{0} ** 32,
};

pub const Process = extern struct {
    /// Creates a kernel thread: allocates a TCB slot and a `kstack_pages`
    /// (4 KiB each) kernel stack, builds a `TaskContext` that begins at
    /// `entry(arg)`, sets state `ready`, and writes the new pid to
    /// `out_pid`. `EINVAL` for a null `entry` / zero or oversized
    /// `kstack_pages`; `ENOMEM` if the table is full or the stack can't
    /// be allocated.
    create_kernel_thread: *const fn (name: [*:0]const u8, entry: usize, arg: usize, kstack_pages: u32, priority: u32, out_pid: *u32) callconv(.c) c_int,
    /// Creates an EL0 process: a TCB with a `kstack_pages` kernel stack
    /// (used only while the process is trapped in the kernel) whose
    /// context, on first switch, installs `ttbr0` (pre-composed
    /// `(asid << 48) | root`) and drops to EL0 at `user_entry` with
    /// SP_EL0 = `user_sp`. The address space itself is the ELF loader's
    /// to build and to tear down. `EINVAL` / `ENOMEM` as
    /// `create_kernel_thread`.
    create_user_process: *const fn (name: [*:0]const u8, ttbr0: u64, user_entry: u64, user_sp: u64, kstack_pages: u32, priority: u32, out_pid: *u32) callconv(.c) c_int,
    /// Creates a forked child: a TCB whose context (built via
    /// `ContextSwitch.init_forked_context`) resumes at EL0 from `frame`
    /// with x0 = 0, in address space `ttbr0`, parented to `parent`.
    create_forked_process: *const fn (name: [*:0]const u8, ttbr0: u64, parent: u32, frame: *const TrapFrame, kstack_pages: u32, priority: u32, out_pid: *u32) callconv(.c) c_int,
    /// Point `pid` at a new address space (updates both the recorded root
    /// and the saved context's TTBR0) -- for `execve`.
    set_address_space: *const fn (pid: u32, ttbr0: u64) callconv(.c) c_int,
    set_parent: *const fn (pid: u32, parent: u32) callconv(.c) c_int,
    /// Frees a thread's kernel stack and releases its TCB slot. `EINVAL`
    /// for an unknown pid; `EBUSY` if the thread is `running`.
    destroy: *const fn (pid: u32) callconv(.c) c_int,
    exists: *const fn (pid: u32) callconv(.c) bool,
    /// Writes `&tcb.context` (usable as `*TaskContext`) to `out`, for the
    /// scheduler to switch through. Valid until `destroy`.
    context_of: *const fn (pid: u32, out: *?*anyopaque) callconv(.c) c_int,
    get_info: *const fn (pid: u32, out: *ProcessInfo) callconv(.c) c_int,
    get_state: *const fn (pid: u32, out: *ProcessState) callconv(.c) c_int,
    /// `EINVAL` for an unknown pid, an out-of-range state, or `.dead`
    /// (use `destroy`).
    set_state: *const fn (pid: u32, state: ProcessState) callconv(.c) c_int,
    get_priority: *const fn (pid: u32, out: *u32) callconv(.c) c_int,
    set_priority: *const fn (pid: u32, priority: u32) callconv(.c) c_int,
    set_exit_code: *const fn (pid: u32, code: i32) callconv(.c) c_int,
    /// Number of live (non-`dead`) TCBs.
    count: *const fn () callconv(.c) u32,
    /// Fills `out_pids[0..max]` with live pids in slot order and writes
    /// the count to `n_out`. `EOVERFLOW` if there are more than `max`
    /// (the first `max` are still written).
    list: *const fn (out_pids: [*]u32, max: u32, n_out: *u32) callconv(.c) c_int,
};

// --- Syscall dispatch (fixed table) ---
//
// `hnsorens.sys.syscall` owns a fixed `SYSCALL_TABLE_SIZE`-entry table and
// registers a `.sync_svc` callback with the exceptions module. Userspace
// (and kernel callers, via `Syscalls.invoke`) use the AArch64 Linux
// convention: number in x8, args in x0..x5, return value in x0 -- so a
// later musl/newlib port needs no shim. Each number below is a well-known
// slot a module registers a handler at; there is no remap/mask layer yet.
pub const SYSCALL_TABLE_SIZE = 512;

// Numbers match Linux/aarch64 (asm-generic/unistd.h) where an equivalent exists.
pub const SYS_getcwd: u32 = 17;
pub const SYS_dup: u32 = 23;
pub const SYS_dup3: u32 = 24;
pub const SYS_fcntl: u32 = 25;
pub const SYS_ioctl: u32 = 29;
pub const SYS_mkdirat: u32 = 34;
pub const SYS_unlinkat: u32 = 35;
pub const SYS_symlinkat: u32 = 36;
pub const SYS_linkat: u32 = 37;
pub const SYS_renameat: u32 = 38;
pub const SYS_ftruncate: u32 = 46;
pub const SYS_faccessat: u32 = 48;
pub const SYS_chdir: u32 = 49;
pub const SYS_fchdir: u32 = 50;
pub const SYS_fchmodat: u32 = 53;
pub const SYS_fchownat: u32 = 54;
pub const SYS_openat: u32 = 56;
pub const SYS_close: u32 = 57;
pub const SYS_pipe2: u32 = 59;
pub const SYS_getdents64: u32 = 61;
pub const SYS_lseek: u32 = 62;
pub const SYS_read: u32 = 63;
pub const SYS_write: u32 = 64;
pub const SYS_readv: u32 = 65;
pub const SYS_writev: u32 = 66;
pub const SYS_pread64: u32 = 67;
pub const SYS_pwrite64: u32 = 68;
pub const SYS_ppoll: u32 = 73;
pub const SYS_readlinkat: u32 = 78;
pub const SYS_newfstatat: u32 = 79;
pub const SYS_fstat: u32 = 80;
pub const SYS_fsync: u32 = 82;
pub const SYS_utimensat: u32 = 88;
pub const SYS_exit: u32 = 93;
pub const SYS_exit_group: u32 = 94;
pub const SYS_set_tid_address: u32 = 96;
pub const SYS_futex: u32 = 98;
pub const SYS_set_robust_list: u32 = 99;
pub const SYS_nanosleep: u32 = 101;
pub const SYS_clock_gettime: u32 = 113;
pub const SYS_clock_getres: u32 = 114;
pub const SYS_clock_nanosleep: u32 = 115;
pub const SYS_sched_yield: u32 = 124;
pub const SYS_sched_getaffinity: u32 = 123;
pub const SYS_kill: u32 = 129;
pub const SYS_tkill: u32 = 130;
pub const SYS_tgkill: u32 = 131;
pub const SYS_rt_sigaction: u32 = 134;
pub const SYS_rt_sigprocmask: u32 = 135;
pub const SYS_rt_sigpending: u32 = 136;
pub const SYS_rt_sigreturn: u32 = 139;
pub const SYS_setpgid: u32 = 154;
pub const SYS_getpgid: u32 = 155;
pub const SYS_getsid: u32 = 156;
pub const SYS_setsid: u32 = 157;
pub const SYS_uname: u32 = 160;
pub const SYS_umask: u32 = 166;
pub const SYS_personality: u32 = 92;
pub const SYS_getpriority: u32 = 141;
pub const SYS_setpriority: u32 = 140;
pub const SYS_sched_setscheduler: u32 = 119;
pub const SYS_sched_getscheduler: u32 = 120;
pub const SYS_sched_getparam: u32 = 121;
pub const SYS_sched_get_priority_max: u32 = 125;
pub const SYS_sched_get_priority_min: u32 = 126;
pub const SYS_times: u32 = 153;
pub const SYS_getgroups: u32 = 158;
pub const SYS_getrusage: u32 = 165;
pub const SYS_getcpu: u32 = 168;
pub const SYS_fadvise64: u32 = 223;
pub const SYS_membarrier: u32 = 283;
pub const SYS_prctl: u32 = 167;
pub const SYS_gettimeofday: u32 = 169;
pub const SYS_getpid: u32 = 172;
pub const SYS_getppid: u32 = 173;
pub const SYS_getuid: u32 = 174;
pub const SYS_geteuid: u32 = 175;
pub const SYS_getgid: u32 = 176;
pub const SYS_getegid: u32 = 177;
pub const SYS_gettid: u32 = 178;
pub const SYS_sysinfo: u32 = 179;
pub const SYS_brk: u32 = 214;
pub const SYS_munmap: u32 = 215;
pub const SYS_mremap: u32 = 216;
pub const SYS_clone: u32 = 220;
pub const SYS_execve: u32 = 221;
pub const SYS_mmap: u32 = 222;
pub const SYS_mprotect: u32 = 226;
pub const SYS_madvise: u32 = 233;
pub const SYS_wait4: u32 = 260;
pub const SYS_prlimit64: u32 = 261;
pub const SYS_getrandom: u32 = 278;
pub const SYS_statx: u32 = 291;
pub const SYS_faccessat2: u32 = 439;

// mmap(2) prot/flags
pub const PROT_NONE: u64 = 0;
pub const PROT_READ: u64 = 1;
pub const PROT_WRITE: u64 = 2;
pub const PROT_EXEC: u64 = 4;
pub const MAP_PRIVATE: u64 = 0x02;
pub const MAP_FIXED: u64 = 0x10;
pub const MAP_ANONYMOUS: u64 = 0x20;

// *at() dirfd sentinel + unlinkat flag
pub const AT_REMOVEDIR: u64 = 0x200;
pub const AT_SYMLINK_NOFOLLOW: u64 = 0x100;
pub const AT_EMPTY_PATH: u64 = 0x1000;

// open() flags and lseek() whence (generic Linux values).
pub const O_RDONLY: u32 = 0;
pub const O_WRONLY: u32 = 1;
pub const O_RDWR: u32 = 2;
pub const O_CREAT: u32 = 0o100;
pub const O_TRUNC: u32 = 0o1000;
pub const O_APPEND: u32 = 0o2000;
pub const O_DIRECTORY: u32 = 0o200000;
pub const AT_FDCWD: i32 = -100;
pub const SEEK_SET: u32 = 0;
pub const SEEK_CUR: u32 = 1;
pub const SEEK_END: u32 = 2;

/// The six general-purpose arguments a syscall handler receives, plus the
/// number it was invoked as.
pub const SyscallArgs = extern struct {
    nr: u64,
    arg: [6]u64,
};

/// A registered handler. The return value is placed in x0 on `eret`;
/// negative means `-errno` by convention (not enforced by the table).
pub const SyscallHandler = *const fn (args: *const SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64;

/// A "raw" handler that gets the whole `TrapFrame` -- for syscalls that
/// must read or rewrite the caller's full register / PC state (`clone`
/// / `fork`, `execve`). May mutate the frame; the return value still
/// lands in x0 afterward. A raw handler registered for a number takes
/// precedence over a plain one.
pub const RawSyscallHandler = *const fn (frame: *TrapFrame, ctx: ?*anyopaque) callconv(.c) i64;

pub const Syscalls = extern struct {
    /// Register `handler` for syscall `nr`. `EINVAL` if `nr >=
    /// SYSCALL_TABLE_SIZE`, `EBUSY` if a handler is already registered.
    register: *const fn (nr: u32, handler: SyscallHandler, ctx: ?*anyopaque) callconv(.c) c_int,
    /// Idempotent -- clearing an empty slot still returns 0. `EINVAL` if
    /// `nr` is out of range.
    unregister: *const fn (nr: u32) callconv(.c) c_int,
    is_registered: *const fn (nr: u32) callconv(.c) bool,
    count: *const fn () callconv(.c) u32,
    /// Run syscall `nr` directly from kernel code (no `svc`). Returns the
    /// handler's value, or `-ENOSYS` if `nr` is unregistered / out of
    /// range. (Raw handlers are not reachable this way -- they need a real
    /// trap frame.)
    invoke: *const fn (nr: u64, a0: u64, a1: u64, a2: u64, a3: u64, a4: u64, a5: u64) callconv(.c) i64,
    /// Register a raw (trap-frame) handler for `nr`; takes precedence over
    /// a plain one. `EINVAL` for a null handler / out-of-range `nr`,
    /// `EBUSY` if one is already registered.
    register_raw: *const fn (nr: u32, handler: RawSyscallHandler, ctx: ?*anyopaque) callconv(.c) c_int,
    unregister_raw: *const fn (nr: u32) callconv(.c) c_int,
};

// --- Cooperative round-robin scheduler ---
//
// `hnsorens.sched.scheduler` keeps a run queue of ready pids (from the
// process module) and switches between them with the context_switch
// module. Cooperative only for now: a task gives up the CPU by calling
// `yield` / `block` / `exit_current`; there is no tick preemption yet
// (that needs a reschedule-after-EOIR hook in the exception path).
//
// `run` is the entry point -- it switches into the first ready task and
// returns to its caller once the run queue drains (every task has
// exited or blocked). The kernel's idle loop is then
// `while (true) { scheduler.run(); wfi(); }`.
pub const Scheduler = extern struct {
    /// Enqueue a ready task. `EINVAL` if the pid is unknown, `EEXIST` if
    /// it is already queued or currently running, `ENOMEM` if the queue
    /// is full.
    admit: *const fn (pid: u32) callconv(.c) c_int,
    /// Remove a queued (not currently running) task from the run queue
    /// without destroying it. `EINVAL` if it isn't queued, `EBUSY` if it
    /// is the running task (use `exit_current`).
    remove: *const fn (pid: u32) callconv(.c) c_int,
    /// pid of the running task, or 0 when called from the bootstrap
    /// context (outside `run`, or with the queue drained).
    current: *const fn () callconv(.c) u32,
    queue_len: *const fn () callconv(.c) u32,
    /// Give up the CPU to the next ready task, re-queuing the caller.
    /// Returns once the caller is scheduled again. No-op if there is no
    /// other ready task.
    yield: *const fn () callconv(.c) void,
    /// Mark the running task blocked and switch away. It won't run again
    /// until `wake`. Returns (once rescheduled). `EINVAL` if called with
    /// no running task.
    block: *const fn () callconv(.c) c_int,
    /// Move a blocked task back to ready and re-queue it. `EINVAL` if the
    /// pid isn't currently blocked.
    wake: *const fn (pid: u32) callconv(.c) c_int,
    /// Set the running task to `zombie`, drop it from rotation, and
    /// switch away. Does NOT return to the calling task.
    exit_current: *const fn () callconv(.c) void,
    /// Switch into the first ready task; returns 0 to the caller once the
    /// run queue has drained. `EBUSY` if already running (no reentry).
    run: *const fn () callconv(.c) c_int,
};

// --- Raw console keyboard input ---
//
// `hnsorens.io.keyboard` drives the receive side of the PL011 UART (the
// only "keyboard" the QEMU virt board has): it unmasks the UART RX
// interrupt, registers an ISR, and hands every received byte, raw, to a
// registered listener. Line discipline is `hnsorens.io.tty`'s job; fd
// plumbing is `hnsorens.fs.fd`'s.
pub const KeyListener = *const fn (byte: u8) callconv(.c) void;

pub const Keyboard = extern struct {
    /// Set the sink for raw RX bytes (invoked from the ISR). One
    /// listener; a later call replaces it. While one is set, bytes go
    /// only to it (not the fallback ring below).
    set_listener: *const fn (cb: KeyListener) callconv(.c) void,
    /// Non-blocking: drain up to `max` bytes from the fallback ring (used
    /// only when no listener is set). Returns the count.
    read: *const fn (buf: [*]u8, max: u64) callconv(.c) u64,
    available: *const fn () callconv(.c) u64,
};

// --- Console line discipline (tty) ---
//
// `hnsorens.io.tty` sits on `keyboard`'s raw byte stream and `serial`'s
// output. In canonical mode it buffers a line, echoes keystrokes, handles
// backspace / ^U / ^D, translates CR->LF, and only lets a `read` complete
// once a full line (or EOF) is ready. In raw mode every byte is delivered
// as it arrives. It owns the block/wake of a process waiting on console
// input.
pub const Tty = extern struct {
    /// Blocking read of console input. Canonical: returns at most one
    /// line's worth (up to `max`), 0 at EOF (^D on an empty line). Raw:
    /// returns as soon as >= 1 byte is available.
    read: *const fn (buf: [*]u8, max: u64) callconv(.c) u64,
    /// Write to the console (goes to `serial`). Returns `len`.
    write: *const fn (buf: [*]const u8, len: u64) callconv(.c) u64,
    /// `canonical` = line-buffered + editing; `echo` = echo keystrokes.
    set_mode: *const fn (canonical: bool, echo: bool) callconv(.c) void,
};

// --- File descriptors / open-file table ---
//
// `hnsorens.fs.fd` owns a per-process fd table and a pool of open-file
// descriptions (each with an offset -- the "file pointer" -- a refcount,
// and a backing: the console tty, or a VFS path). It owns the
// `read`/`write`/`openat`/`close`/`lseek`/`dup` syscalls, dispatching by
// fd to the right backing. The `Fd` vtable below is for the process
// modules to set up / fork / tear down a table.
pub const Fd = extern struct {
    /// Give `pid` a fresh table with fds 0/1/2 bound to the console.
    /// `EEXIST` if it already has one.
    open_defaults: *const fn (pid: u32) callconv(.c) c_int,
    /// Give `child` a copy of `parent`'s table (open-file descriptions
    /// shared, refcounts bumped) -- fork semantics.
    fork_table: *const fn (parent: u32, child: u32) callconv(.c) c_int,
    /// Close every fd and release `pid`'s table (idempotent-ish: `EINVAL`
    /// if `pid` has no table).
    clear_table: *const fn (pid: u32) callconv(.c) c_int,
};

// --- Userspace ELF loading ---
//
// `hnsorens.proc.elf_loader` reads a static AArch64 `ET_EXEC` binary from
// the VFS, builds a user address space (a copy of the bootloader's TTBR0
// identity map -- so device MMIO stays reachable while the kernel handles
// a syscall -- plus the program's PT_LOAD segments and a stack, all above
// the identity-mapped range), and creates a ready EL0 process for it. It
// also owns the small set of process syscalls a first userspace program
// needs (write to the console, getpid, exit, sched_yield).
pub const Elf = extern struct {
    /// Loads `path` into a fresh user address space and creates a ready
    /// user process; writes its pid to `out_pid`. The caller admits it to
    /// the scheduler. `ENOENT` if the path doesn't resolve, `ENOEXEC` for
    /// a malformed / non-AArch64 / non-`ET_EXEC` image, `ENOMEM` on
    /// resource exhaustion.
    load: *const fn (path: [*:0]const u8, out_pid: *u32) callconv(.c) c_int,
    /// Tears down a loaded process: frees its user page tables and every
    /// backing frame, then destroys the TCB. `EINVAL` for an unknown pid,
    /// `EBUSY` if it is still `running`.
    unload: *const fn (pid: u32) callconv(.c) c_int,
};

// --- Synchronous exception / fault / async trap dispatch (AArch64 EL1) ---
//
// `hnsorens.arch.exceptions` owns VBAR_EL1 and a full 16-entry vector
// table. Every entry saves a `TrapFrame`, decodes what happened, and
// hands it to whichever module registered a callback for that class. GIC
// IRQ handling is just an `.irq` callback registered by `gic_v3`; a page
// fault is a `.data_abort` callback (the process/fault module); a syscall
// is an `.sync_svc` callback (the syscall module).

/// Full integer register state at an exception, saved by the vector
/// trampoline. A handler may mutate any field; the changes take effect on
/// `eret` -- e.g. advance `elr` past a faulting instruction, or write a
/// syscall's return value into `x[0]`.
pub const TrapFrame = extern struct {
    /// x0..x30.
    x: [31]u64,
    /// Stack pointer the exception was taken on (SP_EL0 for a lower-EL
    /// trap, the pre-frame SP_EL1 otherwise).
    sp: u64,
    /// ELR_EL1 -- the address `eret` returns to.
    elr: u64,
    /// SPSR_EL1 -- PSTATE restored on `eret`.
    spsr: u64,
    /// ESR_EL1 -- syndrome. EC is bits 31:26; ISS is bits 24:0.
    esr: u64,
    /// FAR_EL1 -- faulting virtual address (valid for aborts / alignment
    /// faults; stale otherwise).
    far: u64,
};

/// Which vector-table entry / decoded cause a callback is registered for.
/// Values are stable array indices.
pub const ExceptionVector = enum(u32) {
    sync_svc = 0,
    sync_data_abort = 1,
    sync_instruction_abort = 2,
    sync_pc_alignment = 3,
    sync_sp_alignment = 4,
    /// Any synchronous EC not broken out above.
    sync_other = 5,
    irq = 6,
    fiq = 7,
    serror = 8,
};

pub const ExceptionOrigin = enum(u32) {
    current_el_sp0 = 0,
    current_el_spx = 1,
    lower_el_aarch64 = 2,
    lower_el_aarch32 = 3,
};

/// A callback's verdict, telling the trampoline what to do next.
pub const ExceptionOutcome = enum(u32) {
    /// `eret` with the (possibly modified) frame.
    handled = 0,
    /// Callback declined; the exceptions module applies its default
    /// policy (skip-and-log for `sync_svc`/`sync_other` and the async
    /// classes; log + halt the core for an unhandled real fault).
    unhandled = 1,
};

pub const ExceptionCallback = *const fn (frame: *TrapFrame, origin: ExceptionOrigin, arg: ?*anyopaque) callconv(.c) ExceptionOutcome;

/// Called on every return to a lower EL (EL0), after the vector callback
/// and default policy, with the about-to-be-restored `TrapFrame`. The
/// signal layer uses this to divert `eret` into a signal handler.
pub const UserReturnHook = *const fn (frame: *TrapFrame) callconv(.c) void;

/// POSIX signals, exported by `hnsorens.sys.signal` (category "signal").
/// The process layer calls these; the module itself owns the syscalls
/// (rt_sigaction/procmask/pending/return, kill/tkill/tgkill), the
/// return-to-EL0 delivery hook and the EL0 fault -> SIGSEGV path.
pub const Signal = extern struct {
    /// Make `sig` pending on `pid` (and wake it if blocked). No-op for an
    /// unknown pid or sig outside 1..64.
    raise: *const fn (pid: u32, sig: u32) callconv(.c) void,
    /// Drop all per-process signal state for `pid` (exit / reap / execve).
    forget: *const fn (pid: u32) callconv(.c) void,
    /// Copy `parent`'s dispositions + blocked mask to `child` (fork).
    fork_inherit: *const fn (parent: u32, child: u32) callconv(.c) void,
};

/// Exported by the exceptions module (category "exceptions").
pub const Exceptions = extern struct {
    /// Install the vector table in VBAR_EL1 on the current core. The
    /// module's own `main` already does this for the boot core.
    init_core: *const fn () callconv(.c) c_int,
    /// Register the sole callback for `vector`. `EBUSY` if one is set,
    /// `EINVAL` for a null callback.
    register_handler: *const fn (vector: ExceptionVector, cb: ExceptionCallback, arg: ?*anyopaque) callconv(.c) c_int,
    /// Remove `vector`'s callback (idempotent -- removing an absent one
    /// still returns 0).
    unregister_handler: *const fn (vector: ExceptionVector) callconv(.c) c_int,
    /// Set (or clear, with null) the sole return-to-EL0 hook.
    set_user_return_hook: *const fn (hook: ?UserReturnHook) callconv(.c) c_int,
};

// --- VirtIO-MMIO bus, block device, GPT, ext2, VFS ---
//
// A storage stack ported from a previous C OS's `things_to_add/*.c`
// (bus_controller.c, blk_device.c, gpt.c, ext2.c, vfs.c). Each stage below
// is its own module talking to the previous one purely through an
// exported/imported vtable, same as every other driver in this codebase --
// the C reference had gpt/ext2 call blk_dev_* and bus_controller_*
// directly as linked functions, which this ABI has no equivalent of.

/// One virtqueue's live state (descriptor table + avail/used rings),
/// shared between whichever module owns the queue's memory (a block
/// device) and the module that actually drives the VirtIO-MMIO transport
/// (the bus controller) -- mirrors the C reference's `virtio_queue_t`
/// being reinterpreted in place across `bus_controller.c` and
/// `blk_device.c`. `desc`/`avail`/`used` are HHDM-mapped virtual addresses
/// (usable as pointers by the owning code); `*_phys` are the matching
/// physical addresses the device is programmed with. Only the bus
/// controller module interprets the pointed-to memory's layout.
pub const VirtioQueue = extern struct {
    size: u16 = 0,
    free_head: u16 = 0,
    last_used_idx: u16 = 0,
    desc: u64 = 0,
    avail: u64 = 0,
    used: u64 = 0,
    desc_phys: u64 = 0,
    avail_phys: u64 = 0,
    used_phys: u64 = 0,
};

/// Generic VirtIO-MMIO transport, exported by the bus controller
/// (category "virtiobus"). `mmio_base` throughout is the identity-mapped
/// physical address of a device's MMIO window (as returned by
/// `find_device`), not a handle -- matches the C reference treating the
/// device base as a plain pointer under its flat `virt_to_phys(x) = x`
/// assumption, which holds here too (TTBR0's flat identity map covers the
/// low physical range MMIO devices live in).
pub const VirtioBus = extern struct {
    find_device: *const fn (device_id: u32) callconv(.c) u64,
    init_device: *const fn (mmio_base: u64) callconv(.c) c_int,
    setup_queue: *const fn (mmio_base: u64, queue_idx: u32, queue: *VirtioQueue) callconv(.c) c_int,
    submit_request: *const fn (mmio_base: u64, req_type: u32, queue: *VirtioQueue, req: ?*const anyopaque, req_len: u64, data: ?*anyopaque, data_len: u64, status: ?*u8) callconv(.c) c_int,
};

/// Block device, exported by the virtio-blk driver (category
/// "blkdevice"). All addressing is in fixed 512-byte sectors (the VirtIO
/// block spec's unit, independent of the device's reported optimal
/// `blk_size`), matching GPT LBA units.
///
/// `read_sectors`/`write_sectors`' `buf` must be a physically-contiguous,
/// HHDM-mapped buffer (e.g. from a `phys_mem.alloc` call, or anything
/// else obtained as `phys + HHDM_OFFSET`) -- it ends up as a VirtIO
/// virtqueue data descriptor's address, computed by subtracting
/// `HHDM_OFFSET` back out (see `phys_mem.virtToPhys`). A plain stack or
/// heap buffer is *not* HHDM-mapped, so that subtraction yields a
/// physical address unrelated to the buffer -- the device will read from
/// or write to the wrong memory instead of erroring, since it has no way
/// to know the address is bogus.
pub const BlkDevice = extern struct {
    create: *const fn (mmio_base: u64, out_dev: *?*anyopaque) callconv(.c) c_int,
    read_sectors: *const fn (dev: ?*anyopaque, lba: u64, buf: ?*anyopaque, sector_count: u64) callconv(.c) c_int,
    write_sectors: *const fn (dev: ?*anyopaque, lba: u64, buf: ?*const anyopaque, sector_count: u64) callconv(.c) c_int,
    flush: *const fn (dev: ?*anyopaque) callconv(.c) c_int,
    get_capacity_sectors: *const fn (dev: ?*anyopaque, sectors_out: *u64) callconv(.c) c_int,
};

/// One parsed GPT partition table entry (category "gpt"'s `read_partitions`
/// output). `name` is the UTF-16LE on-disk name narrowed to ASCII/Latin-1
/// and NUL-terminated -- adequate for the plain-ASCII labels this OS uses.
pub const GptPartition = extern struct {
    type_guid: [16]u8 = [_]u8{0} ** 16,
    unique_guid: [16]u8 = [_]u8{0} ** 16,
    first_lba: u64 = 0,
    last_lba: u64 = 0,
    attributes: u64 = 0,
    name: [37]u8 = [_]u8{0} ** 37,
};

/// GPT partition table reader, exported by the gpt module (category
/// "gpt"). `read_partitions` fills up to `max_partitions` entries of
/// `out_partitions` (caller-owned -- no allocation crosses the ABI
/// boundary) and writes the number actually found to `count_out`.
pub const Gpt = extern struct {
    read_partitions: *const fn (dev: ?*anyopaque, out_partitions: [*]GptPartition, max_partitions: u32, count_out: *u32) callconv(.c) c_int,
};

pub const EXT2_FT_UNKNOWN: u8 = 0;
pub const EXT2_FT_REG_FILE: u8 = 1;
pub const EXT2_FT_DIR: u8 = 2;
pub const EXT2_FT_CHRDEV: u8 = 3;
pub const EXT2_FT_BLKDEV: u8 = 4;
pub const EXT2_FT_FIFO: u8 = 5;
pub const EXT2_FT_SOCK: u8 = 6;
pub const EXT2_FT_SYMLINK: u8 = 7;

/// The well-known inode number of an ext2 filesystem's root directory.
pub const EXT2_ROOT_INO: u32 = 2;

/// Maximum bytes in one path component (`EXT2_NAME_LEN`).
pub const EXT2_NAME_LEN: u32 = 255;

// --- `Ext2Stat.mode`'s file-type bits (the high nibble of `st_mode`,
// POSIX `S_IFMT` values) -- distinct from `EXT2_FT_*` above, which is the
// smaller, separate encoding directory entries use. ---
pub const EXT2_S_IFSOCK: u16 = 0xC000;
pub const EXT2_S_IFLNK: u16 = 0xA000;
pub const EXT2_S_IFREG: u16 = 0x8000;
pub const EXT2_S_IFBLK: u16 = 0x6000;
pub const EXT2_S_IFDIR: u16 = 0x4000;
pub const EXT2_S_IFCHR: u16 = 0x2000;
pub const EXT2_S_IFIFO: u16 = 0x1000;
pub const EXT2_S_IFMT: u16 = 0xF000;

/// One directory entry as returned by `Ext2.dir_read` / `Vfs.list_dir`.
/// `name` is NUL-terminated; `name_len` gives its length without walking
/// it. Iteration is by flat integer `index` rather than an opaque cursor
/// so no per-listing state has to be allocated and owned across the ABI
/// boundary -- see `Ext2.dir_read`'s doc comment.
pub const Ext2DirEntry = extern struct {
    inode: u32 = 0,
    file_type: u8 = 0,
    name_len: u8 = 0,
    name: [256]u8 = [_]u8{0} ** 256,
};

pub const Ext2Stat = extern struct {
    inode_num: u32 = 0,
    mode: u16 = 0,
    size: u64 = 0,
    links_count: u16 = 0,
    atime: u32 = 0,
    mtime: u32 = 0,
    ctime: u32 = 0,
    /// Device number for `EXT2_FT_CHRDEV`/`EXT2_FT_BLKDEV` inodes (encoded
    /// `major`/`minor`, caller-defined); 0 for every other file type.
    rdev: u32 = 0,
};

/// AArch64 Linux `struct stat` (== `struct kstat64`), 128 bytes. What
/// `fstat`/`newfstatat` write into the caller's buffer; musl copies it
/// verbatim into its own `struct stat`.
pub const KStat = extern struct {
    st_dev: u64 = 0,
    st_ino: u64 = 0,
    st_mode: u32 = 0,
    st_nlink: u32 = 0,
    st_uid: u32 = 0,
    st_gid: u32 = 0,
    st_rdev: u64 = 0,
    __pad1: u64 = 0,
    st_size: i64 = 0,
    st_blksize: i32 = 0,
    __pad2: i32 = 0,
    st_blocks: i64 = 0,
    st_atime: i64 = 0,
    st_atime_nsec: i64 = 0,
    st_mtime: i64 = 0,
    st_mtime_nsec: i64 = 0,
    st_ctime: i64 = 0,
    st_ctime_nsec: i64 = 0,
    __unused: [2]u32 = .{ 0, 0 },
};

/// Linux `struct utsname` -- six 65-byte NUL-padded fields.
pub const UtsName = extern struct {
    sysname: [65]u8 = [_]u8{0} ** 65,
    nodename: [65]u8 = [_]u8{0} ** 65,
    release: [65]u8 = [_]u8{0} ** 65,
    version: [65]u8 = [_]u8{0} ** 65,
    machine: [65]u8 = [_]u8{0} ** 65,
    domainname: [65]u8 = [_]u8{0} ** 65,
};

// getdents64 d_type values (Linux DT_*).
pub const DT_UNKNOWN: u8 = 0;
pub const DT_FIFO: u8 = 1;
pub const DT_CHR: u8 = 2;
pub const DT_DIR: u8 = 4;
pub const DT_BLK: u8 = 6;
pub const DT_REG: u8 = 8;
pub const DT_LNK: u8 = 10;
pub const DT_SOCK: u8 = 12;

// fcntl cmds
pub const F_DUPFD: u64 = 0;
pub const F_GETFD: u64 = 1;
pub const F_SETFD: u64 = 2;
pub const F_GETFL: u64 = 3;
pub const F_SETFL: u64 = 4;
pub const F_DUPFD_CLOEXEC: u64 = 1030;
pub const FD_CLOEXEC: u64 = 1;

// ioctl cmds musl needs for isatty / stdio line-buffering
pub const TCGETS: u64 = 0x5401;
pub const TCSETS: u64 = 0x5402;
pub const TCSETSW: u64 = 0x5403;
pub const TCSETSF: u64 = 0x5404;
pub const TIOCGWINSZ: u64 = 0x5413;
pub const TIOCSWINSZ: u64 = 0x5414;
pub const TIOCGPGRP: u64 = 0x540F;
pub const TIOCSPGRP: u64 = 0x5410;
pub const FIONREAD: u64 = 0x541B;

/// ext2 filesystem driver, exported by the ext2 module (category "ext2").
/// `fs` is an opaque mount handle from `mount`. Every path-shaped
/// operation here works in terms of `(dir_inode, name)` rather than a
/// path string -- multi-component path walking is the VFS module's job,
/// same layering as the C reference's `ext2.c` (inode-number based) vs.
/// `vfs.c` (path-string based).
pub const Ext2 = extern struct {
    mount: *const fn (dev: ?*anyopaque, partition_start_lba: u64, partition_end_lba: u64, out_fs: *?*anyopaque) callconv(.c) c_int,
    unmount: *const fn (fs: ?*anyopaque) callconv(.c) c_int,
    root_inode: *const fn (fs: ?*anyopaque) callconv(.c) u32,

    lookup: *const fn (fs: ?*anyopaque, dir_inode: u32, name: [*:0]const u8, inode_out: *u32, file_type_out: *u8) callconv(.c) c_int,
    stat: *const fn (fs: ?*anyopaque, inode_num: u32, out: *Ext2Stat) callconv(.c) c_int,

    file_create: *const fn (fs: ?*anyopaque, dir_inode: u32, name: [*:0]const u8, mode: u16, inode_out: *u32) callconv(.c) c_int,
    file_delete: *const fn (fs: ?*anyopaque, dir_inode: u32, name: [*:0]const u8) callconv(.c) c_int,
    file_read: *const fn (fs: ?*anyopaque, inode_num: u32, offset: u64, buf: ?*anyopaque, count: u64, bytes_read_out: *u64) callconv(.c) c_int,
    file_write: *const fn (fs: ?*anyopaque, inode_num: u32, offset: u64, buf: ?*const anyopaque, count: u64, bytes_written_out: *u64) callconv(.c) c_int,
    file_truncate: *const fn (fs: ?*anyopaque, inode_num: u32, length: u64) callconv(.c) c_int,

    dir_create: *const fn (fs: ?*anyopaque, parent_inode: u32, name: [*:0]const u8, mode: u16, inode_out: *u32) callconv(.c) c_int,
    dir_delete: *const fn (fs: ?*anyopaque, parent_inode: u32, name: [*:0]const u8) callconv(.c) c_int,
    /// Lists the `index`-th valid entry of `dir_inode` (0-based, in on-disk
    /// order). Returns `ENOENT` once `index` runs past the last entry --
    /// the caller's iteration loop just counts up until it sees that.
    dir_read: *const fn (fs: ?*anyopaque, dir_inode: u32, index: u32, entry_out: *Ext2DirEntry) callconv(.c) c_int,

    rename: *const fn (fs: ?*anyopaque, old_dir_inode: u32, old_name: [*:0]const u8, new_dir_inode: u32, new_name: [*:0]const u8) callconv(.c) c_int,

    /// Creates a symlink at `(dir_inode, name)` pointing at `target`
    /// (stored verbatim, not validated or resolved -- a dangling or
    /// relative target is legal, same as POSIX `symlink()`). A "fast"
    /// symlink (`target` fits in the inode's own block pointers, <= 60
    /// bytes) allocates no data block; longer targets fall back to a
    /// "slow" symlink using one, same as the on-disk format itself.
    symlink_create: *const fn (fs: ?*anyopaque, dir_inode: u32, name: [*:0]const u8, target: [*:0]const u8, inode_out: *u32) callconv(.c) c_int,
    /// Reads `inode_num`'s symlink target into `buf` (up to `buf_len`
    /// bytes, NOT NUL-terminated) and writes its length to `len_out`.
    /// Returns `EINVAL` if `inode_num` is not a symlink.
    symlink_read: *const fn (fs: ?*anyopaque, inode_num: u32, buf: [*]u8, buf_len: u64, len_out: *u64) callconv(.c) c_int,

    /// Creates a character device, block device, FIFO, or socket special
    /// file (`file_type` one of `EXT2_FT_CHRDEV`/`BLKDEV`/`FIFO`/`SOCK`).
    /// `dev` is the encoded device number, meaningful only for
    /// CHRDEV/BLKDEV (ignored otherwise, but still recorded) -- read back
    /// via `Ext2Stat.rdev`. Zero data blocks are ever allocated for any
    /// of these; content lives entirely in the inode.
    mknod: *const fn (fs: ?*anyopaque, dir_inode: u32, name: [*:0]const u8, mode: u16, file_type: u8, dev: u32, inode_out: *u32) callconv(.c) c_int,
};

/// Whole-tree path resolver over `Ext2`, exported by the vfs module
/// (category "vfs"). Mounts the ext2 partition itself (see the vfs
/// module's `main`) rather than requiring a caller to drive
/// find-device/init/gpt/mount first, since there is exactly one root
/// filesystem in this design -- a future multi-mount VFS would need a
/// mount-table argument here, but nothing in this OS needs that yet.
///
/// `resolve`/`read`/`write`/`create`/`mkdir`/`stat`/`list_dir` all follow
/// symlinks at every path component, including a trailing one (matching
/// POSIX `open`/`stat`); `remove`, `lstat`, and `readlink` act on the
/// final component itself without following it (matching POSIX
/// `unlink`/`lstat`/`readlink`) -- otherwise you could never remove a
/// symlink, only what it points at, and `readlink` would just recurse
/// into whatever the link resolves to instead of reporting it. A path
/// with more than `MAX_SYMLINK_DEPTH` (see the vfs module) symlink hops
/// anywhere in it fails with `ELOOP`.
pub const Vfs = extern struct {
    resolve: *const fn (path: [*:0]const u8, inode_out: *u32, file_type_out: *u8) callconv(.c) c_int,
    read: *const fn (path: [*:0]const u8, offset: u64, buf: ?*anyopaque, count: u64, bytes_read_out: *u64) callconv(.c) c_int,
    write: *const fn (path: [*:0]const u8, offset: u64, buf: ?*const anyopaque, count: u64, bytes_written_out: *u64) callconv(.c) c_int,
    create: *const fn (path: [*:0]const u8, mode: u16) callconv(.c) c_int,
    mkdir: *const fn (path: [*:0]const u8, mode: u16) callconv(.c) c_int,
    remove: *const fn (path: [*:0]const u8) callconv(.c) c_int,
    list_dir: *const fn (path: [*:0]const u8, index: u32, entry_out: *Ext2DirEntry) callconv(.c) c_int,
    rename: *const fn (old_path: [*:0]const u8, new_path: [*:0]const u8) callconv(.c) c_int,

    stat: *const fn (path: [*:0]const u8, out: *Ext2Stat) callconv(.c) c_int,
    lstat: *const fn (path: [*:0]const u8, out: *Ext2Stat) callconv(.c) c_int,

    symlink: *const fn (target: [*:0]const u8, link_path: [*:0]const u8) callconv(.c) c_int,
    /// Reads the target of the symlink at `path` itself (not followed)
    /// into `buf` (up to `buf_len` bytes, NOT NUL-terminated), writing its
    /// length to `len_out`. `EINVAL` if `path` isn't a symlink.
    readlink: *const fn (path: [*:0]const u8, buf: ?*anyopaque, buf_len: u64, len_out: *u64) callconv(.c) c_int,
    mknod: *const fn (path: [*:0]const u8, mode: u16, file_type: u8, dev: u32) callconv(.c) c_int,
    /// Truncate (or zero-extend) the regular file at `path` to `length`.
    truncate: *const fn (path: [*:0]const u8, length: u64) callconv(.c) c_int,
};

// --- Module metadata linking ---

/// One entry in the `.kmodule.tests` section: a named test function.
/// A test returns TEST_PASS / TEST_FAIL / TEST_SKIP.
pub const TestEntry = extern struct {
    name: [*:0]const u8,
    func: *const fn () callconv(.c) i32,
};

pub const EXPORT_SECTION_PREFIX = ".kmodule.export.";
pub const IMPORT_SECTION_PREFIX = ".kmodule.import.";
pub const CONFIG_SECTION_PREFIX = ".kmodule.config.";
pub const TESTS_SECTION = ".kmodule.tests";

pub fn exportSection(comptime type_name: []const u8, comptime instance_name: []const u8) []const u8 {
    return EXPORT_SECTION_PREFIX ++ type_name ++ "." ++ instance_name;
}

/// A module's import placeholder names only the interface *type* it needs
/// (e.g. "vmm"), never a specific provider instance -- which module
/// actually fills it in is decided by the [Dependencies] wiring in
/// kernel.ini, not baked into the module's own code.
pub fn importSection(comptime type_name: []const u8) []const u8 {
    return IMPORT_SECTION_PREFIX ++ type_name;
}

/// A module's config placeholder names the parse `kind` ("int"/"bool"/
/// "str" -- how the bootloader should interpret the kernel.ini text value)
/// and the `key` (the name kernel.ini's `[module.config]` section uses).
/// The section's byte size (recovered from the ELF itself, not from this
/// name) tells the bootloader exactly how many bytes to overwrite.
pub fn configSection(comptime kind: []const u8, comptime key: []const u8) []const u8 {
    return CONFIG_SECTION_PREFIX ++ kind ++ "." ++ key;
}
