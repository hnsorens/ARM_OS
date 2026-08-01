const std = @import("std");
const uefi = std.os.uefi;

const PhysicalAddress = u64;

// PLO11 UART Register (ARM Versatile Express base)
const UART0_BASE = 0x09000000;

// Register offsets
const UARTDR = 0x00;     // Data register
const UARTFR = 0x18;     // Flag register
const UARTIBRD = 0x24;   // Integer baud rate divisor
const UARTFBRD = 0x28;   // Fractional baud rate divisor
const UARTLCR_H = 0x2C;  // Line control register
const UARTCR = 0x30;     // Control register
const UARTIMSC = 0x38;   // Interrupt mask set/clear register

// Flag bits
const UARTFR_TXFF = ( 1 << 5 );  // Transmit FIFO full
const UARTFR_RXFE = ( 1 << 4 );  // Receive FIFO empty
                                     
fn str_len(str: [*:0]const u8) usize {
    var len: usize = 0;
    while (str[len] != 0) {
        len += 1;
    }
    return len;
}

inline fn MMIO_write(reg: PhysicalAddress, data: u32) void {
    const ptr: *volatile u32 = @ptrFromInt(reg);
    ptr.* = data;
}

inline fn MMIO_read(reg: PhysicalAddress) u32 {
    const ptr: *volatile u32 = @ptrFromInt(reg);
    return ptr.*;
}

fn uart_putc(c: u8) void {
    // Wait until transmit FIFO has space
    while ((MMIO_read(UART0_BASE + UARTFR) & UARTFR_TXFF) != 0) {}

    // Write Character
    MMIO_write(UART0_BASE + UARTDR, c);
}

fn uart_puts(str: [*:0]const u8, n: usize) usize {
    for (str[0..n]) |char| {
        uart_putc(char);
    }
    return n;
}

pub fn boot_log(str: [*:0]const u8, n: usize) usize {
    _ = uart_puts("[Boot] ", 7);
    _ = uart_puts(str, n);
    return n + 7;
}

pub fn ok_log(str: [*:0]const u8, n: usize) usize {
    _ = uart_puts("[ \x1B[92mOK\x1B[0m ]", 17);
    _ = uart_puts(str, n);
    return n + 17;
}

pub fn fail_log(str: [*:0]const u8, n: usize) usize {
    _ = uart_puts("[\x1B[31mFAIL\x1B[0m] ", 17);
    _ = uart_puts(str, n);
    return n + 17;
}

pub fn boot_log_start() void {
    // Disable UART
    MMIO_write(UART0_BASE + UARTCR, 0);

    // Set baud rate to 115200
    // (UARTCLK = 24MHz, baud = 115200)
    MMIO_write(UART0_BASE + UARTIBRD, 13);
    MMIO_write(UART0_BASE + UARTFBRD, 1);

    // Set line control: 8 bits, no penalty, 1 stop bit, FIFOs enabled
    MMIO_write(UART0_BASE + UARTLCR_H, ( 1 << 4 ) | ( 1 << 5 ) | ( 1 << 6 ));

    // Enable UART, enable transmit & receive
    MMIO_write(UART0_BASE + UARTCR, ( 1 << 0 ) | ( 1 << 8 ) | ( 1 << 9 ));

    _ = ok_log("UART serial out initialized\n", 28);
}
