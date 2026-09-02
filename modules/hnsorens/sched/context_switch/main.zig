//! Raw AArch64 cooperative context switching, exporting `ContextSwitch`
//! (category "contextswitch"). A `TaskContext` is the callee-saved
//! register file (x19-x28), frame pointer (x29), link register (x30) and
//! stack pointer -- exactly what the AArch64 procedure call standard
//! requires a call to preserve. `switch_to` saves that set for the
//! current execution, loads it for another, swaps SP, and `ret`s into the
//! restored x30; the effect is a coroutine yield between two stacks.
//!
//! No C reference existed (HendOS's context switch was x86_64
//! `iretq`-frame based); this follows the AArch64 plan in ROADMAP.md.
//! It is the bottom of the scheduling stack: `proc.process` builds
//! `TaskContext`s, `sched.scheduler` decides which to run, this module is
//! the only thing that actually changes which code is executing.
const abi = @import("abi");
const kernel_fmt = @import("kernel_fmt");

pub const serial_if = abi.importInterface(abi.Serial);

// --- The switch primitives, in assembly -------------------------------
//
// TaskContext is 14 packed u64s, so field offsets are 8*index:
//   x19..x28 -> 0,8,16,24,32,40,48,56,64,72
//   fp (x29) -> 80   lr (x30) -> 88   sp -> 96   ttbr0 -> 104
// x9 is caller-saved (a scratch temp); nothing else is touched that the
// C ABI would expect preserved. TTBR0_EL1 is saved and restored around
// every switch (with an isb) so kernel tasks keep the identity map and a
// dead user process's page tables become safe to free.
comptime {
    asm (
        \\.global ctxsw_switch_to
        \\ctxsw_switch_to:
        \\    stp x19, x20, [x0, #0]
        \\    stp x21, x22, [x0, #16]
        \\    stp x23, x24, [x0, #32]
        \\    stp x25, x26, [x0, #48]
        \\    stp x27, x28, [x0, #64]
        \\    stp x29, x30, [x0, #80]
        \\    mov x9, sp
        \\    str x9, [x0, #96]
        \\    mrs x9, ttbr0_el1
        \\    str x9, [x0, #104]
        \\    ldp x19, x20, [x1, #0]
        \\    ldp x21, x22, [x1, #16]
        \\    ldp x23, x24, [x1, #32]
        \\    ldp x25, x26, [x1, #48]
        \\    ldp x27, x28, [x1, #64]
        \\    ldp x29, x30, [x1, #80]
        \\    ldr x9, [x1, #96]
        \\    mov sp, x9
        \\    ldr x9, [x1, #104]
        \\    msr ttbr0_el1, x9
        \\    isb
        \\    ret
        \\
        \\.global ctxsw_jump_to
        \\ctxsw_jump_to:
        \\    ldp x19, x20, [x0, #0]
        \\    ldp x21, x22, [x0, #16]
        \\    ldp x23, x24, [x0, #32]
        \\    ldp x25, x26, [x0, #48]
        \\    ldp x27, x28, [x0, #64]
        \\    ldp x29, x30, [x0, #80]
        \\    ldr x9, [x0, #96]
        \\    mov sp, x9
        \\    ldr x9, [x0, #104]
        \\    msr ttbr0_el1, x9
        \\    isb
        \\    ret
        \\
        \\// First code a freshly init'd kernel context runs.
        \\// init_kernel_context leaves the entry fn in the x19 slot and its
        \\// argument in x20; the restore sequence above has just loaded
        \\// both. If entry returns, fall through to the Zig park routine.
        \\.global ctxsw_trampoline
        \\ctxsw_trampoline:
        \\    mov x0, x20
        \\    blr x19
        \\    bl ctxsw_task_returned
        \\9:
        \\    wfi
        \\    b 9b
        \\
        \\// First code a freshly init'd user context runs, on its kernel
        \\// stack with its address space (TTBR0) already installed by the
        \\// restore sequence. init_user_context leaves the user entry PC in
        \\// x19 and the user SP in x20. Drop to EL0.
        \\.global ctxsw_user_trampoline
        \\ctxsw_user_trampoline:
        \\    msr elr_el1, x19
        \\    msr sp_el0, x20
        \\    mov x0, #0
        \\    msr spsr_el1, x0        // EL0t, DAIF clear, NZCV 0
        \\    // Don't leak kernel register contents into EL0.
        \\    mov x1, #0
        \\    mov x2, #0
        \\    mov x3, #0
        \\    mov x4, #0
        \\    mov x5, #0
        \\    mov x6, #0
        \\    mov x7, #0
        \\    mov x8, #0
        \\    mov x9, #0
        \\    mov x10, #0
        \\    mov x11, #0
        \\    mov x12, #0
        \\    mov x13, #0
        \\    mov x14, #0
        \\    mov x15, #0
        \\    mov x16, #0
        \\    mov x17, #0
        \\    mov x18, #0
        \\    mov x19, #0
        \\    mov x20, #0
        \\    mov x29, #0
        \\    mov x30, #0
        \\    eret
    );
}

extern fn ctxsw_switch_to(save: *abi.TaskContext, restore: *const abi.TaskContext) callconv(.c) void;
extern fn ctxsw_jump_to(restore: *const abi.TaskContext) callconv(.c) noreturn;
extern var ctxsw_trampoline: u8;
extern var ctxsw_user_trampoline: u8;

/// Reached only if a task's entry function returns (a bug in this design
/// -- kernel tasks are expected to loop or block forever, and userspace
/// tasks exit via a syscall). Logs and lets the asm caller WFI-park.
export fn ctxsw_task_returned() callconv(.c) void {
    kernel_fmt.print(serial_if, "[context_switch] task entry returned with nowhere to go; parking core\n", .{});
}

const MIN_STACK_BYTES: u64 = 4096;

fn currentTtbr0() u64 {
    return asm volatile ("mrs %[v], ttbr0_el1"
        : [v] "=r" (-> u64),
    );
}

pub fn initKernelContext(ctx: *abi.TaskContext, entry: usize, arg: usize, stack_top: u64) callconv(.c) c_int {
    if (entry == 0 or stack_top < MIN_STACK_BYTES) return abi.EINVAL;
    ctx.* = .{};
    ctx.sp = stack_top & ~@as(u64, 0xF);
    ctx.lr = @intFromPtr(&ctxsw_trampoline);
    // A kernel task keeps whatever address space is live now (the
    // bootloader's TTBR0 identity map).
    ctx.ttbr0 = currentTtbr0();
    // Picked up by ctxsw_trampoline after the restore sequence loads the
    // callee-saved slots.
    ctx.x19 = entry;
    ctx.x20 = arg;
    return 0;
}

pub fn initUserContext(ctx: *abi.TaskContext, kstack_top: u64, ttbr0: u64, user_entry: u64, user_sp: u64) callconv(.c) c_int {
    if (user_entry == 0 or ttbr0 == 0 or kstack_top < MIN_STACK_BYTES) return abi.EINVAL;
    ctx.* = .{};
    ctx.sp = kstack_top & ~@as(u64, 0xF);
    ctx.lr = @intFromPtr(&ctxsw_user_trampoline);
    ctx.ttbr0 = ttbr0;
    // Picked up by ctxsw_user_trampoline.
    ctx.x19 = user_entry;
    ctx.x20 = user_sp & ~@as(u64, 0xF);
    return 0;
}

pub fn main(boot_info_ptr: *anyopaque) void {
    _ = boot_info_ptr;
    kernel_fmt.print(serial_if, "[context_switch] ready\n", .{});
}

comptime {
    abi.exportInterface("aarch64", abi.ContextSwitch, .{
        .init_kernel_context = initKernelContext,
        .init_user_context = initUserContext,
        .switch_to = ctxsw_switch_to,
        .jump_to = ctxsw_jump_to,
    });
}

comptime {
    _ = @import("test.zig");
}
