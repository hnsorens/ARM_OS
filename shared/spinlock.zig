//! A minimal AArch64 spinlock, factored out of `gic_v3` and `timer`, which
//! both carried byte-identical `spinLock`/`spinUnlock` functions.
const std = @import("std");

pub const SpinLock = extern struct {
    state: u64 = 0,

    // The exclusive-store status result of `stlxr` must be a 32-bit (W)
    // register no matter the data size, but Zig's inline asm has no
    // template modifier to request a sub-register view of a named operand
    // (`%w[name]` doesn't parse, and `"r"` constraints always allocate a
    // full X register here) -- so the status is bound to a hardcoded
    // physical register (w9) instead, with x9 declared clobbered.
    pub fn lock(self: *SpinLock) void {
        _ = asm volatile (
            \\1: ldaxr %[ret], [%[addr]]
            \\   cbnz %[ret], 1b
            \\   stlxr w9, %[one], [%[addr]]
            \\   cbnz w9, 1b
            : [ret] "=&r" (-> u64),
            : [addr] "r" (&self.state),
              [one] "r" (@as(u64, 1)),
            : .{ .memory = true, .x9 = true });
    }

    pub fn unlock(self: *SpinLock) void {
        asm volatile ("stlr %[zero], [%[addr]]"
            :
            : [zero] "r" (@as(u64, 0)),
              [addr] "r" (&self.state),
            : .{ .memory = true });
    }
};
