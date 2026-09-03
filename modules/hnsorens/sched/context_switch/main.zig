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
// TaskContext field offsets (the leading run is 8*index):
//   x19..x28 -> 0,8,16,24,32,40,48,56,64,72
//   fp (x29) -> 80   lr (x30) -> 88   sp -> 96   ttbr0 -> 104
//   tpidr_el0 -> 112   fpsr -> 120   fpcr -> 128
//   q0..q31   -> 144,160,...,624   (16-aligned; 32 * 16 bytes = 512)
// x9 is caller-saved (a scratch temp); nothing else is touched that the
// C ABI would expect preserved. TTBR0_EL1 is saved and restored around
// every switch (with an isb) so kernel tasks keep the identity map and a
// dead user process's page tables become safe to free. TPIDR_EL0 and the
// whole FP/SIMD state travel with the task too -- musl keeps `errno` off
// TPIDR_EL0 and its string ops are NEON, so a switch that dropped either
// would corrupt userspace.
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
        \\    mrs x9, tpidr_el0
        \\    str x9, [x0, #112]
        \\    mrs x9, fpsr
        \\    str x9, [x0, #120]
        \\    mrs x9, fpcr
        \\    str x9, [x0, #128]
        \\    stp q0,  q1,  [x0, #144]
        \\    stp q2,  q3,  [x0, #176]
        \\    stp q4,  q5,  [x0, #208]
        \\    stp q6,  q7,  [x0, #240]
        \\    stp q8,  q9,  [x0, #272]
        \\    stp q10, q11, [x0, #304]
        \\    stp q12, q13, [x0, #336]
        \\    stp q14, q15, [x0, #368]
        \\    stp q16, q17, [x0, #400]
        \\    stp q18, q19, [x0, #432]
        \\    stp q20, q21, [x0, #464]
        \\    stp q22, q23, [x0, #496]
        \\    stp q24, q25, [x0, #528]
        \\    stp q26, q27, [x0, #560]
        \\    stp q28, q29, [x0, #592]
        \\    stp q30, q31, [x0, #624]
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
        \\    ldr x9, [x1, #112]
        \\    msr tpidr_el0, x9
        \\    ldr x9, [x1, #120]
        \\    msr fpsr, x9
        \\    ldr x9, [x1, #128]
        \\    msr fpcr, x9
        \\    ldp q0,  q1,  [x1, #144]
        \\    ldp q2,  q3,  [x1, #176]
        \\    ldp q4,  q5,  [x1, #208]
        \\    ldp q6,  q7,  [x1, #240]
        \\    ldp q8,  q9,  [x1, #272]
        \\    ldp q10, q11, [x1, #304]
        \\    ldp q12, q13, [x1, #336]
        \\    ldp q14, q15, [x1, #368]
        \\    ldp q16, q17, [x1, #400]
        \\    ldp q18, q19, [x1, #432]
        \\    ldp q20, q21, [x1, #464]
        \\    ldp q22, q23, [x1, #496]
        \\    ldp q24, q25, [x1, #528]
        \\    ldp q26, q27, [x1, #560]
        \\    ldp q28, q29, [x1, #592]
        \\    ldp q30, q31, [x1, #624]
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
        \\    ldr x9, [x0, #112]
        \\    msr tpidr_el0, x9
        \\    ldr x9, [x0, #120]
        \\    msr fpsr, x9
        \\    ldr x9, [x0, #128]
        \\    msr fpcr, x9
        \\    ldp q0,  q1,  [x0, #144]
        \\    ldp q2,  q3,  [x0, #176]
        \\    ldp q4,  q5,  [x0, #208]
        \\    ldp q6,  q7,  [x0, #240]
        \\    ldp q8,  q9,  [x0, #272]
        \\    ldp q10, q11, [x0, #304]
        \\    ldp q12, q13, [x0, #336]
        \\    ldp q14, q15, [x0, #368]
        \\    ldp q16, q17, [x0, #400]
        \\    ldp q18, q19, [x0, #432]
        \\    ldp q20, q21, [x0, #464]
        \\    ldp q22, q23, [x0, #496]
        \\    ldp q24, q25, [x0, #528]
        \\    ldp q26, q27, [x0, #560]
        \\    ldp q28, q29, [x0, #592]
        \\    ldp q30, q31, [x0, #624]
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
        \\
        \\// A forked child's first code. init_forked_context leaves SP
        \\// pointing at a TrapFrame copy on the child's kernel stack (with
        \\// x0 pre-forced to 0). Reload the full EL0 state from it and
        \\// return to userspace at the parent's fork() call site.
        \\.global ctxsw_forked_trampoline
        \\ctxsw_forked_trampoline:
        \\    ldr x0, [sp, #256]
        \\    msr elr_el1, x0
        \\    ldr x0, [sp, #264]
        \\    msr spsr_el1, x0
        \\    ldr x0, [sp, #248]
        \\    msr sp_el0, x0
        \\    ldp x2, x3,   [sp, #16]
        \\    ldp x4, x5,   [sp, #32]
        \\    ldp x6, x7,   [sp, #48]
        \\    ldp x8, x9,   [sp, #64]
        \\    ldp x10, x11, [sp, #80]
        \\    ldp x12, x13, [sp, #96]
        \\    ldp x14, x15, [sp, #112]
        \\    ldp x16, x17, [sp, #128]
        \\    ldp x18, x19, [sp, #144]
        \\    ldp x20, x21, [sp, #160]
        \\    ldp x22, x23, [sp, #176]
        \\    ldp x24, x25, [sp, #192]
        \\    ldp x26, x27, [sp, #208]
        \\    ldp x28, x29, [sp, #224]
        \\    ldr x30,      [sp, #240]
        \\    ldp x0, x1,   [sp, #0]
        \\    add sp, sp, #288
        \\    eret
    );
}

extern fn ctxsw_switch_to(save: *abi.TaskContext, restore: *const abi.TaskContext) callconv(.c) void;
extern fn ctxsw_jump_to(restore: *const abi.TaskContext) callconv(.c) noreturn;
extern var ctxsw_trampoline: u8;
extern var ctxsw_user_trampoline: u8;
extern var ctxsw_forked_trampoline: u8;

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

/// Snapshot the caller's live TPIDR_EL0 + FP/SIMD state into `ctx`. Used
/// by `initForkedContext` so a forked child resumes at EL0 with the
/// parent's exact thread pointer (errno!) and FP register file -- the
/// trap frame only carried the general-purpose registers.
fn captureFpAndTls(ctx: *abi.TaskContext) void {
    ctx.tpidr = asm volatile ("mrs %[v], tpidr_el0"
        : [v] "=r" (-> u64),
    );
    ctx.fpsr = asm volatile ("mrs %[v], fpsr"
        : [v] "=r" (-> u64),
    );
    ctx.fpcr = asm volatile ("mrs %[v], fpcr"
        : [v] "=r" (-> u64),
    );
    asm volatile (
        \\ stp q0,  q1,  [%[v], #0]
        \\ stp q2,  q3,  [%[v], #32]
        \\ stp q4,  q5,  [%[v], #64]
        \\ stp q6,  q7,  [%[v], #96]
        \\ stp q8,  q9,  [%[v], #128]
        \\ stp q10, q11, [%[v], #160]
        \\ stp q12, q13, [%[v], #192]
        \\ stp q14, q15, [%[v], #224]
        \\ stp q16, q17, [%[v], #256]
        \\ stp q18, q19, [%[v], #288]
        \\ stp q20, q21, [%[v], #320]
        \\ stp q22, q23, [%[v], #352]
        \\ stp q24, q25, [%[v], #384]
        \\ stp q26, q27, [%[v], #416]
        \\ stp q28, q29, [%[v], #448]
        \\ stp q30, q31, [%[v], #480]
        :
        : [v] "r" (&ctx.v),
        : .{ .memory = true });
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

pub fn initForkedContext(ctx: *abi.TaskContext, kstack_top: u64, ttbr0: u64, frame: *const abi.TrapFrame) callconv(.c) c_int {
    if (ttbr0 == 0 or kstack_top < MIN_STACK_BYTES) return abi.EINVAL;
    const slot = (kstack_top - @sizeOf(abi.TrapFrame)) & ~@as(u64, 0xF);
    const dst: *abi.TrapFrame = @ptrFromInt(slot);
    dst.* = frame.*;
    dst.x[0] = 0; // child's fork() returns 0
    ctx.* = .{};
    ctx.sp = slot;
    ctx.lr = @intFromPtr(&ctxsw_forked_trampoline);
    ctx.ttbr0 = ttbr0;
    // The child inherits the parent's thread pointer + FP register file;
    // the trap frame only held x0..x30.
    captureFpAndTls(ctx);
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
        .init_forked_context = initForkedContext,
        .switch_to = ctxsw_switch_to,
        .jump_to = ctxsw_jump_to,
    });
}

comptime {
    _ = @import("test.zig");
}
