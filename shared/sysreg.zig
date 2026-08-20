//! Declarative AArch64 system-register (mrs/msr) access -- `mmio.zig`'s
//! sibling for register state that lives in-core rather than in memory.
//! Same idea (comptime type generation, packed structs for named
//! bitfields, zero runtime cost), but a register is addressed by its
//! architectural name instead of a byte offset, so there's no `base`
//! parameter to thread through -- system registers are inherently
//! per-core, not addressable via a pointer.
//!
//! All-asm, so unlike `mmio.zig` there's no host-testable logic here worth
//! a native `test {}` block (`modify`'s field-merge is the same code
//! already covered by `mmio.Reg.modify`'s tests) -- it isn't wired into
//! the native unit-test build, since `mrs`/`msr` won't compile for a
//! non-AArch64 host target anyway.

const std = @import("std");

/// A single AArch64 system register. `T` is either a plain unsigned
/// integer or a `packed struct(uN)` describing its named bitfields; `name`
/// is the register's assembly mnemonic (e.g. "sctlr_el1", "ICC_SRE_EL1").
pub fn Reg(comptime T: type, comptime name: []const u8) type {
    const bits = @bitSizeOf(T);
    comptime {
        if (bits != 32 and bits != 64) {
            @compileError("sysreg.Reg only supports 32- or 64-bit registers, got " ++ @typeName(T));
        }
    }
    const Int = std.meta.Int(.unsigned, bits);

    return struct {
        pub inline fn read() T {
            const v = asm volatile ("mrs %[out], " ++ name
                : [out] "=r" (-> Int),
            );
            return @bitCast(v);
        }

        pub inline fn write(value: T) void {
            const v: Int = @bitCast(value);
            asm volatile ("msr " ++ name ++ ", %[v]"
                :
                : [v] "r" (v),
            );
        }

        /// Read-modify-write: merges only the named fields present in
        /// `fields` into the register's current value. `T` must be a
        /// struct type.
        pub inline fn modify(fields: anytype) void {
            var v = read();
            inline for (@typeInfo(@TypeOf(fields)).@"struct".fields) |f| {
                @field(v, f.name) = @field(fields, f.name);
            }
            write(v);
        }
    };
}
