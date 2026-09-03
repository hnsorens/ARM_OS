//! AArch64 EL1 exception dispatch, exporting `Exceptions` (category
//! "exceptions"). Owns VBAR_EL1 and a full 16-entry vector table. Every
//! entry saves a `TrapFrame`, `exc_dispatch` decodes the source (which of
//! the 16 slots) and, for a synchronous exception, the cause (ESR_EL1.EC),
//! then calls whichever module registered a callback for that class.
//!
//! This replaces `gic_v3`'s old private IRQ-only table: GIC IRQ handling
//! is now a `.irq` callback that `gic_v3` registers here, a page fault is
//! a `.data_abort` callback, a syscall is a `.sync_svc` callback. Nothing
//! else in the tree installs VBAR.
//!
//! No C reference (HendOS was x86_64 IDT-based) -- this follows the
//! AArch64 exception model directly.
const abi = @import("abi");
const kernel_fmt = @import("kernel_fmt");
const sysreg = @import("sysreg");
const spinlock = @import("spinlock");

pub const serial_if = abi.importInterface(abi.Serial);

// --- The vector table + save/restore trampoline ----------------------
//
// TrapFrame is 36 packed u64s = 288 bytes (16-aligned). Field offsets:
//   x0..x30 -> 0,8,...,240   sp -> 248   elr -> 256
//   spsr -> 264   esr -> 272   far -> 280
// Each of the 16 vector slots is 128 bytes; the stub there just carves
// the frame, stashes x0/x1 (scratch), loads its slot index into x0, and
// branches to the common path.
comptime {
    asm (
        \\.macro VEC_ENTRY idx
        \\.balign 0x80
        \\    sub sp, sp, #288
        \\    stp x0, x1, [sp, #0]
        \\    mov x0, #\idx
        \\    b exc_common
        \\.endm
        \\
        \\.balign 2048
        \\.global exc_vector_table
        \\exc_vector_table:
        \\    VEC_ENTRY 0   // Current EL, SP_EL0    : Synchronous
        \\    VEC_ENTRY 1   // Current EL, SP_EL0    : IRQ
        \\    VEC_ENTRY 2   // Current EL, SP_EL0    : FIQ
        \\    VEC_ENTRY 3   // Current EL, SP_EL0    : SError
        \\    VEC_ENTRY 4   // Current EL, SP_ELx    : Synchronous
        \\    VEC_ENTRY 5   // Current EL, SP_ELx    : IRQ
        \\    VEC_ENTRY 6   // Current EL, SP_ELx    : FIQ
        \\    VEC_ENTRY 7   // Current EL, SP_ELx    : SError
        \\    VEC_ENTRY 8   // Lower EL, AArch64     : Synchronous
        \\    VEC_ENTRY 9   // Lower EL, AArch64     : IRQ
        \\    VEC_ENTRY 10  // Lower EL, AArch64     : FIQ
        \\    VEC_ENTRY 11  // Lower EL, AArch64     : SError
        \\    VEC_ENTRY 12  // Lower EL, AArch32     : Synchronous
        \\    VEC_ENTRY 13  // Lower EL, AArch32     : IRQ
        \\    VEC_ENTRY 14  // Lower EL, AArch32     : FIQ
        \\    VEC_ENTRY 15  // Lower EL, AArch32     : SError
        \\
        \\exc_common:
        \\    // x0 = slot index; x0/x1 already saved at [sp,#0].
        \\    stp x2, x3,   [sp, #16]
        \\    stp x4, x5,   [sp, #32]
        \\    stp x6, x7,   [sp, #48]
        \\    stp x8, x9,   [sp, #64]
        \\    stp x10, x11, [sp, #80]
        \\    stp x12, x13, [sp, #96]
        \\    stp x14, x15, [sp, #112]
        \\    stp x16, x17, [sp, #128]
        \\    stp x18, x19, [sp, #144]
        \\    stp x20, x21, [sp, #160]
        \\    stp x22, x23, [sp, #176]
        \\    stp x24, x25, [sp, #192]
        \\    stp x26, x27, [sp, #208]
        \\    stp x28, x29, [sp, #224]
        \\    str x30,      [sp, #240]
        \\    // interrupted SP: SP_EL0 for a lower-EL trap (idx >= 8),
        \\    // else the SP from just before this frame was carved.
        \\    mrs x1, sp_el0
        \\    add x2, sp, #288
        \\    cmp x0, #8
        \\    csel x1, x1, x2, ge
        \\    str x1, [sp, #248]
        \\    mrs x1, elr_el1
        \\    str x1, [sp, #256]
        \\    mrs x1, spsr_el1
        \\    str x1, [sp, #264]
        \\    mrs x1, esr_el1
        \\    str x1, [sp, #272]
        \\    mrs x1, far_el1
        \\    str x1, [sp, #280]
        \\    mov x1, sp          // arg1 = frame*
        \\    bl exc_dispatch     // arg0 = slot index (still in x0)
        \\    ldr x1, [sp, #256]
        \\    msr elr_el1, x1
        \\    ldr x1, [sp, #264]
        \\    msr spsr_el1, x1
        \\    // Restore SP_EL0. Essential for a lower-EL return: a task
        \\    // that blocked in a syscall while another EL0 task ran must
        \\    // get its own user SP back, not the other task's. Harmless
        \\    // for a current-EL return (EL1h ignores SP_EL0).
        \\    ldr x1, [sp, #248]
        \\    msr sp_el0, x1
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

extern var exc_vector_table: u8;

const Vbar = sysreg.Reg(u64, "vbar_el1");

// --- Callback registry ----------------------------------------------

const NUM_VECTORS = 9; // == number of abi.ExceptionVector tags

const Slot = struct {
    cb: ?abi.ExceptionCallback = null,
    arg: ?*anyopaque = null,
};

var s_handlers: [NUM_VECTORS]Slot = [_]Slot{.{}} ** NUM_VECTORS;
var s_lock: spinlock.SpinLock = .{};

fn ecToVector(ec: u32) abi.ExceptionVector {
    return switch (ec) {
        0x15 => .sync_svc, // SVC from AArch64
        0x20, 0x21 => .sync_instruction_abort,
        0x24, 0x25 => .sync_data_abort,
        0x22 => .sync_pc_alignment,
        0x26 => .sync_sp_alignment,
        else => .sync_other,
    };
}

fn originOf(slot_index: u64) abi.ExceptionOrigin {
    return switch (slot_index / 4) {
        0 => .current_el_sp0,
        1 => .current_el_spx,
        2 => .lower_el_aarch64,
        else => .lower_el_aarch32,
    };
}

/// Called from `exc_common`. `slot_index` is 0..15 (which vector entry);
/// `frame` points at the saved `TrapFrame` on the exception stack.
export fn exc_dispatch(slot_index: u64, frame: *abi.TrapFrame) callconv(.c) void {
    const column = slot_index % 4; // 0 sync, 1 irq, 2 fiq, 3 serror
    const origin = originOf(slot_index);

    const vector: abi.ExceptionVector = switch (column) {
        1 => .irq,
        2 => .fiq,
        3 => .serror,
        else => ecToVector(@truncate((frame.esr >> 26) & 0x3F)),
    };

    s_lock.lock();
    const slot = s_handlers[@intFromEnum(vector)];
    s_lock.unlock();

    if (slot.cb) |cb| {
        if (cb(frame, origin, slot.arg) == .handled) return;
    }
    applyDefault(vector, frame, origin);
}

fn applyDefault(vector: abi.ExceptionVector, frame: *abi.TrapFrame, origin: abi.ExceptionOrigin) void {
    kernel_fmt.print(serial_if, "[exceptions] unhandled {s}: ESR={x} FAR={x} ELR={x} origin={s}\n", .{
        @tagName(vector), frame.esr, frame.far, frame.elr, @tagName(origin),
    });
    switch (vector) {
        // For SVC the CPU already set ELR_EL1 to the instruction *after*
        // the `svc`, so simply returning resumes past it -- no need (and
        // it would be wrong) to touch ELR here. Async traps likewise just
        // resume.
        .sync_svc, .irq, .fiq, .serror => {},
        // A real fault (or an unrecognised synchronous EC) with nobody to
        // handle it: ELR points *at* the trapping instruction, so
        // returning would loop on it forever. Park the core so the
        // failure is visible instead.
        .sync_data_abort, .sync_instruction_abort, .sync_pc_alignment, .sync_sp_alignment, .sync_other => {
            kernel_fmt.print(serial_if, "[exceptions] fatal fault with no handler; halting core\n", .{});
            while (true) asm volatile ("wfe");
        },
    }
}

// --- Exported interface -------------------------------------------------

pub fn initCore() callconv(.c) c_int {
    Vbar.write(@intFromPtr(&exc_vector_table));
    asm volatile ("isb");
    return 0;
}

pub fn registerHandler(vector: abi.ExceptionVector, cb: ?abi.ExceptionCallback, arg: ?*anyopaque) callconv(.c) c_int {
    const idx = @intFromEnum(vector);
    if (idx >= NUM_VECTORS or cb == null) return abi.EINVAL;

    s_lock.lock();
    defer s_lock.unlock();
    if (s_handlers[idx].cb != null) return abi.EBUSY;
    s_handlers[idx] = .{ .cb = cb, .arg = arg };
    return 0;
}

pub fn unregisterHandler(vector: abi.ExceptionVector) callconv(.c) c_int {
    const idx = @intFromEnum(vector);
    if (idx >= NUM_VECTORS) return abi.EINVAL;

    s_lock.lock();
    defer s_lock.unlock();
    s_handlers[idx] = .{};
    return 0;
}

pub fn main(boot_info_ptr: *anyopaque) void {
    _ = boot_info_ptr;
    _ = initCore();
    kernel_fmt.print(serial_if, "[exceptions] vector table installed at {x}\n", .{@intFromPtr(&exc_vector_table)});
}

comptime {
    abi.exportInterface("aarch64", abi.Exceptions, .{
        .init_core = initCore,
        .register_handler = registerHandler,
        .unregister_handler = unregisterHandler,
    });
}

comptime {
    _ = @import("test.zig");
}
