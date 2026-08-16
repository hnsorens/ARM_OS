//! PL011 UART driver, exporting the `Serial` interface (category "serial").
//!
//! The C reference driver (modules/hnsorens/io/serial_debug) bundled a
//! ~700-line hand-rolled printf engine into this module, because C has
//! nothing better available in a freestanding context. Zig does: `std.fmt`
//! already provides comptime-checked formatting, so this driver only needs
//! to move already-formatted bytes -- see `shared/kernel_fmt.zig` for the
//! formatting half, used by any module holding a `Serial` import.
const abi = @import("abi");
const kernel_test = @import("kernel_test");

// PL011 registers (ARM Versatile Express base, matches QEMU virt machine).
// Same physical address the bootloader's own UART driver uses -- reachable
// from module code too, since the bootloader's low-half identity map
// (TTBR0) covers it regardless of which half a module's own code runs in.
const UART0_BASE: u64 = 0x09000000;
const UARTDR = 0x00;
const UARTFR = 0x18;
const UARTFR_TXFF: u32 = 1 << 5;

inline fn mmioWrite(reg: u64, data: u32) void {
    const ptr: *volatile u32 = @ptrFromInt(reg);
    ptr.* = data;
}

inline fn mmioRead(reg: u64) u32 {
    const ptr: *volatile u32 = @ptrFromInt(reg);
    return ptr.*;
}

fn uartPutc(c: u8) void {
    while ((mmioRead(UART0_BASE + UARTFR) & UARTFR_TXFF) != 0) {}
    mmioWrite(UART0_BASE + UARTDR, c);
}

/// Translates '\n' to "\r\n" to preserve terminal line-tracking, matching
/// the C driver.
fn write(ptr: [*]const u8, len: usize) callconv(.c) usize {
    for (ptr[0..len]) |c| {
        if (c == '\n') uartPutc('\r');
        uartPutc(c);
    }
    return len;
}

comptime {
    abi.exportInterface("pl011", abi.Serial, .{ .write = write });
}

// A plain literal, not an import: importing our own exported "serial"
// category here would make this module depend on itself in the registry's
// dependency graph (an instant cycle -- initOne would recurse into itself
// forever until it hits MAX_DEPTH).
const self_serial: abi.Serial = .{ .write = write };

fn testWriteReturnsLength() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = &self_serial };
    const msg = "serial_debug self-test\n";
    t.expectEqual(@src(), write(msg.ptr, msg.len), msg.len);
    return t.result();
}

comptime {
    abi.kernelTest("write_returns_length", &testWriteReturnsLength);
}
