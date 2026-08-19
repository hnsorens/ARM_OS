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
const mmio = @import("mmio");

// PL011 registers (ARM Versatile Express base, matches QEMU virt machine).
// Same physical address the bootloader's own UART driver uses -- reachable
// from module code too, since the bootloader's low-half identity map
// (TTBR0) covers it regardless of which half a module's own code runs in.
const UART0_BASE: u64 = 0x09000000;

const UartFr = packed struct(u32) {
    _reserved0: u5 = 0,
    /// UARTFR.TXFF (bit 5): transmit FIFO full.
    tx_full: bool = false,
    _reserved1: u26 = 0,
};

const Uart = struct {
    const Dr = mmio.Reg(u32, 0x00);
    const Fr = mmio.Reg(UartFr, 0x18);
};

fn uartPutc(c: u8) void {
    while (Uart.Fr.read(UART0_BASE).tx_full) {}
    Uart.Dr.write(UART0_BASE, c);
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

pub const uartWrite = write;

comptime {
    _ = @import("test.zig");
}
