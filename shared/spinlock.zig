//! A minimal AArch64 spinlock, factored out of `gic_v3` and `timer`, which
//! both carried byte-identical `spinLock`/`spinUnlock` functions.
const std = @import("std");

pub const SpinLock = extern struct {
    state: u64 = 0,
    /// DAIF as it was just before the current holder acquired the lock;
    /// restored by `unlock`. Valid only while `state != 0`.
    saved_daif: u64 = 0,

    // The exclusive-store status result of `stlxr` must be a 32-bit (W)
    // register no matter the data size, but Zig's inline asm has no
    // template modifier to request a sub-register view of a named operand
    // (`%w[name]` doesn't parse, and `"r"` constraints always allocate a
    // full X register here) -- so the status is bound to a hardcoded
    // physical register (w9) instead, with x9 declared clobbered.
    //
    // `lock` masks IRQ+FIQ first: a spinlock that is also taken from an
    // interrupt handler (the UART RX ISR -> tty; a timer callback ->
    // scheduler) would otherwise deadlock the moment an IRQ preempts a
    // holder on the same core. Restored by `unlock` (assumes strict
    // LIFO lock/unlock nesting, which the kernel observes).
    pub fn lock(self: *SpinLock) void {
        const daif = asm volatile ("mrs %[v], daif"
            : [v] "=r" (-> u64),
        );
        asm volatile ("msr daifset, #3" ::: .{ .memory = true });
        _ = asm volatile (
            \\1: ldaxr %[ret], [%[addr]]
            \\   cbnz %[ret], 1b
            \\   stlxr w9, %[one], [%[addr]]
            \\   cbnz w9, 1b
            : [ret] "=&r" (-> u64),
            : [addr] "r" (&self.state),
              [one] "r" (@as(u64, 1)),
            : .{ .memory = true, .x9 = true });
        self.saved_daif = daif;
    }

    pub fn unlock(self: *SpinLock) void {
        const daif = self.saved_daif;
        asm volatile ("stlr %[zero], [%[addr]]"
            :
            : [zero] "r" (@as(u64, 0)),
              [addr] "r" (&self.state),
            : .{ .memory = true });
        asm volatile ("msr daif, %[v]"
            :
            : [v] "r" (daif),
            : .{ .memory = true });
    }
};
