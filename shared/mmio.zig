//! Declarative MMIO register definitions for kernel drivers.
//!
//! Every driver that talks to a memory-mapped peripheral ends up writing
//! the same three things by hand: a `*volatile T` cast for each register,
//! an offset-from-index formula for repeated registers (ISENABLERn,
//! IPRIORITYRn, ...), and shift/mask code for fields that share a
//! register with others. This file gives each of those a single,
//! reusable comptime constructor, so a driver's register map reads as a
//! flat list of declarations instead of scattered accessor functions:
//!
//!   const Gicd = struct {
//!       const Ctlr = mmio.Reg(packed struct(u32) {
//!           enable_grp0: bool = false,
//!           enable_grp1: bool = false,
//!           _reserved: u2 = 0,
//!           are_ns: bool = false,
//!           _reserved2: u27 = 0,
//!       }, 0x0000);
//!       const Isenabler = mmio.IndexedField(bool, 0x0100, u32);
//!       const Ipriorityr = mmio.IndexedField(u8, 0x0400, u32);
//!       const Irouter = mmio.RegArray(u64, 0x6000, 8);
//!   };
//!
//!   Gicd.Ctlr.modify(gicd_base, .{ .are_ns = true });
//!   Gicd.Isenabler.writeOneHot(gicd_base, irq);
//!   const prio = Gicd.Ipriorityr.read(gicd_base, irq);
//!
//! Every accessor takes the peripheral's runtime base address as its
//! first argument rather than binding one at comptime -- some bases
//! (e.g. a GICv3 redistributor frame) are only known per-core at
//! runtime, so binding a fixed instance wouldn't cover that case.
//!
//! Three building blocks, matching the three shapes registers come in:
//!   - `Reg`: one register at a fixed offset. Give it a plain integer
//!     type for a scalar register, or a `packed struct(uN)` to name its
//!     bitfields.
//!   - `RegArray`: a run of identically-shaped, full-width registers
//!     spaced `stride` bytes apart (e.g. one IROUTER per SPI).
//!   - `IndexedField`: a run of *sub-register-width* elements packed
//!     several-per-register and addressed by one flat index (e.g. one
//!     priority byte per IRQ, four IRQs per IPRIORITYR word).

const std = @import("std");

/// A single register of type `T` at byte offset `offset` from a
/// peripheral's base address. `T` is typically a plain unsigned integer
/// (u8/u16/u32/u64) for a scalar register, or a `packed struct(uN)` when
/// the register bit-packs several named fields.
pub fn Reg(comptime T: type, comptime offset: u64) type {
    return struct {
        pub const Offset = offset;
        pub const Type = T;

        pub inline fn ptr(base: u64) *volatile T {
            return @ptrFromInt(base + offset);
        }

        pub inline fn read(base: u64) T {
            return ptr(base).*;
        }

        pub inline fn write(base: u64, val: T) void {
            ptr(base).* = val;
        }

        /// Read-modify-write: reads the register, overwrites only the
        /// fields named in `fields` (e.g. `.{ .enable = true }`), and
        /// writes the result back. `T` must be a packed struct.
        pub inline fn modify(base: u64, fields: anytype) void {
            var val = read(base);
            inline for (@typeInfo(@TypeOf(fields)).@"struct".fields) |f| {
                @field(val, f.name) = @field(fields, f.name);
            }
            write(base, val);
        }

        /// OR `mask` into the register. `T` must be a plain integer
        /// (bitwise ops aren't defined on packed structs) -- for
        /// bitfield registers use `modify` instead.
        pub inline fn setBits(base: u64, mask: T) void {
            write(base, read(base) | mask);
        }

        /// AND `~mask` into the register (clear every bit set in `mask`).
        pub inline fn clearBits(base: u64, mask: T) void {
            write(base, read(base) & ~mask);
        }
    };
}

/// A run of identically-shaped, full-width registers spaced `stride`
/// bytes apart starting at `base_offset` -- one whole register per
/// index, as opposed to `IndexedField`'s several-elements-per-register
/// packing. Matches things like GICD_IROUTER<n> (one u64 per SPI).
pub fn RegArray(comptime T: type, comptime base_offset: u64, comptime stride: u64) type {
    return struct {
        pub const Type = T;

        pub inline fn ptr(base: u64, index: u64) *volatile T {
            return @ptrFromInt(base + base_offset + index * stride);
        }

        pub inline fn read(base: u64, index: u64) T {
            return ptr(base, index).*;
        }

        pub inline fn write(base: u64, index: u64, val: T) void {
            ptr(base, index).* = val;
        }
    };
}

fn ElemInt(comptime ElemT: type) type {
    return if (ElemT == bool) u1 else ElemT;
}

/// A run of `ElemT`-wide elements packed several-per-register into
/// `RegT`-wide registers starting at `base_offset`, addressed by one
/// flat index that spans register boundaries (e.g. index 35 with 4
/// elements/register lands in the 8th register, sub-slot 3). Matches
/// GICv3-style arrays: one priority byte per IRQ (`IndexedField(u8, ...,
/// u32)`, 4 IRQs/register), one 2-bit trigger config per IRQ
/// (`IndexedField(u2, ..., u32)`, 16 IRQs/register), or one enable bit
/// per IRQ (`IndexedField(bool, ..., u32)`, 32 IRQs/register).
///
/// `ElemT` may be `bool`, or any unsigned integer type whose width
/// divides `RegT`'s width evenly.
pub fn IndexedField(comptime ElemT: type, comptime base_offset: u64, comptime RegT: type) type {
    const elem_bits = if (ElemT == bool) 1 else @bitSizeOf(ElemT);
    const reg_bits = @bitSizeOf(RegT);
    if (reg_bits % elem_bits != 0) {
        @compileError("IndexedField: register width must be a multiple of the element width");
    }
    const per_reg = reg_bits / elem_bits;
    const ShiftT = std.math.Log2Int(RegT);
    const RawT = ElemInt(ElemT);

    return struct {
        pub const elements_per_register = per_reg;

        inline fn regPtr(base: u64, index: u64) *volatile RegT {
            const reg_idx = index / per_reg;
            return @ptrFromInt(base + base_offset + reg_idx * @sizeOf(RegT));
        }

        inline fn shiftFor(index: u64) ShiftT {
            return @intCast((index % per_reg) * elem_bits);
        }

        inline fn elemMask() RegT {
            // elem_bits == reg_bits (per_reg == 1) would overflow a
            // same-width shift, so that width-1 case is handled
            // separately rather than computing `(1 << elem_bits) - 1`.
            return if (per_reg == 1) ~@as(RegT, 0) else (@as(RegT, 1) << @intCast(elem_bits)) - 1;
        }

        pub inline fn read(base: u64, index: u64) ElemT {
            const raw: RawT = @truncate(regPtr(base, index).* >> shiftFor(index));
            return if (ElemT == bool) raw != 0 else raw;
        }

        /// Read-modify-write: preserves every other element packed into
        /// the same register.
        pub inline fn write(base: u64, index: u64, val: ElemT) void {
            const raw: RawT = if (ElemT == bool) (if (val) 1 else 0) else val;
            const shift = shiftFor(index);
            var reg = regPtr(base, index).*;
            reg &= ~(elemMask() << shift);
            reg |= @as(RegT, raw) << shift;
            regPtr(base, index).* = reg;
        }

        /// Writes this element's bit as a one-hot value, leaving every
        /// other bit in the register zero (rather than read-modify-write
        /// via `write`). This is the correct access pattern for
        /// write-1-to-set / write-1-to-clear registers (e.g. GICD's
        /// ISENABLER/ICENABLER), where a 0 bit means "leave alone", not
        /// "clear" -- a plain RMW would clear every other pending bit in
        /// the same register. Requires a 1-bit element (`bool` or `u1`).
        pub inline fn writeOneHot(base: u64, index: u64) void {
            if (elem_bits != 1) @compileError("writeOneHot requires a 1-bit element type (bool or u1)");
            regPtr(base, index).* = @as(RegT, 1) << shiftFor(index);
        }
    };
}

test "Reg: scalar read/write round-trips" {
    var backing: u32 = 0;
    const base = @intFromPtr(&backing);
    const R = Reg(u32, 0);

    R.write(base, 0x1234_5678);
    try std.testing.expectEqual(@as(u32, 0x1234_5678), R.read(base));
    try std.testing.expectEqual(@as(u32, 0x1234_5678), backing);
}

test "Reg: offset is honored against the base address" {
    var backing: [2]u32 = .{ 0, 0 };
    const base = @intFromPtr(&backing);
    const R = Reg(u32, 4);

    R.write(base, 0xAAAA_AAAA);
    try std.testing.expectEqual(@as(u32, 0), backing[0]);
    try std.testing.expectEqual(@as(u32, 0xAAAA_AAAA), backing[1]);
}

test "Reg: modify merges only the named fields" {
    const Ctlr = packed struct(u32) {
        enable: bool = false,
        _reserved: u3 = 0,
        are_ns: bool = false,
        _reserved2: u27 = 0,
    };
    var backing: u32 = 0;
    const base = @intFromPtr(&backing);
    const R = Reg(Ctlr, 0);

    R.write(base, .{ .enable = true });
    R.modify(base, .{ .are_ns = true });

    const val = R.read(base);
    try std.testing.expect(val.enable);
    try std.testing.expect(val.are_ns);
}

test "Reg: setBits/clearBits" {
    var backing: u32 = 0b0001;
    const base = @intFromPtr(&backing);
    const R = Reg(u32, 0);

    R.setBits(base, 0b0110);
    try std.testing.expectEqual(@as(u32, 0b0111), backing);
    R.clearBits(base, 0b0011);
    try std.testing.expectEqual(@as(u32, 0b0100), backing);
}

test "RegArray: indexes by stride, independent of element width" {
    var backing: [4]u64 = .{ 0, 0, 0, 0 };
    const base = @intFromPtr(&backing);
    const R = RegArray(u64, 0, 8);

    R.write(base, 2, 0xDEAD_BEEF);
    try std.testing.expectEqual(@as(u64, 0xDEAD_BEEF), R.read(base, 2));
    try std.testing.expectEqual(@as(u64, 0), R.read(base, 0));
}

test "IndexedField: byte-per-element packing (priority-register style)" {
    var backing: u32 = 0;
    const base = @intFromPtr(&backing);
    const F = IndexedField(u8, 0, u32);

    F.write(base, 0, 0xAA);
    F.write(base, 3, 0xBB);
    try std.testing.expectEqual(@as(u8, 0xAA), F.read(base, 0));
    try std.testing.expectEqual(@as(u8, 0xBB), F.read(base, 3));
    try std.testing.expectEqual(@as(u8, 0), F.read(base, 1));
    // Same underlying register (index 0..3 all land in word 0).
    try std.testing.expectEqual(@as(u32, 0xBB00_00AA), backing);
}

test "IndexedField: 2-bit-per-element packing (icfgr-style)" {
    var backing: u32 = 0;
    const base = @intFromPtr(&backing);
    const F = IndexedField(u2, 0, u32);

    F.write(base, 5, 0b10);
    try std.testing.expectEqual(@as(u2, 0b10), F.read(base, 5));
    try std.testing.expectEqual(@as(u32, 0b10 << 10), backing);
}

test "IndexedField: bool elements cross register boundaries at the flat index" {
    var backing: [2]u32 = .{ 0, 0 };
    const base = @intFromPtr(&backing);
    const F = IndexedField(bool, 0, u32);

    F.write(base, 31, true);
    F.write(base, 32, true);
    try std.testing.expect(F.read(base, 31));
    try std.testing.expect(F.read(base, 32));
    try std.testing.expectEqual(@as(u32, 1 << 31), backing[0]);
    try std.testing.expectEqual(@as(u32, 1), backing[1]);
}

test "IndexedField: writeOneHot sets only its own bit, ignoring prior contents" {
    var backing: u32 = 0xFFFF_FFFF;
    const base = @intFromPtr(&backing);
    const F = IndexedField(bool, 0, u32);

    F.writeOneHot(base, 5);
    try std.testing.expectEqual(@as(u32, 1 << 5), backing);
}
