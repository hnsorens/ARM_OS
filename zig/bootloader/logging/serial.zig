//! PL011 UART logging used before, during, and after MMU enable.
//!
//! Message format matches the C bootloader's telemetry markers
//! ("[Boot] ", "[ OK ] ", "[FAIL] ", "[TEST PASS] - <module> <test>", ...)
//! so the QEMU serial test harness can grep for them.

const PhysicalAddress = u64;

// PL011 UART registers (ARM Versatile Express base, matches QEMU virt machine).
const UART0_BASE: PhysicalAddress = 0x09000000;

const UARTDR = 0x00; // Data register
const UARTFR = 0x18; // Flag register
const UARTIBRD = 0x24; // Integer baud rate divisor
const UARTFBRD = 0x28; // Fractional baud rate divisor
const UARTLCR_H = 0x2C; // Line control register
const UARTCR = 0x30; // Control register

const UARTFR_TXFF: u32 = 1 << 5; // Transmit FIFO full

inline fn mmioWrite(reg: PhysicalAddress, data: u32) void {
    const ptr: *volatile u32 = @ptrFromInt(reg);
    ptr.* = data;
}

inline fn mmioRead(reg: PhysicalAddress) u32 {
    const ptr: *volatile u32 = @ptrFromInt(reg);
    return ptr.*;
}

fn putc(c: u8) void {
    while ((mmioRead(UART0_BASE + UARTFR) & UARTFR_TXFF) != 0) {}
    mmioWrite(UART0_BASE + UARTDR, c);
}

fn puts(str: []const u8) void {
    for (str) |c| putc(c);
}

fn putHex(val: u64) void {
    const hex_chars = "0123456789ABCDEF";
    var buf: [18]u8 = undefined; // "0x" + 16 hex digits
    buf[0] = '0';
    buf[1] = 'x';
    var v = val;
    var idx: usize = 18;
    while (idx > 2) {
        idx -= 1;
        buf[idx] = hex_chars[v & 0xF];
        v >>= 4;
    }
    puts(&buf);
}

fn putDec(val: u64) void {
    if (val == 0) {
        putc('0');
        return;
    }
    var buf: [20]u8 = undefined; // u64 max is 20 digits
    var v = val;
    var i: usize = 0;
    while (v > 0) : (v /= 10) {
        buf[i] = @as(u8, @intCast(v % 10)) + '0';
        i += 1;
    }
    while (i > 0) {
        i -= 1;
        putc(buf[i]);
    }
}

pub fn bootLog(msg: []const u8) void {
    puts("[Boot] ");
    puts(msg);
}

pub fn okLog(msg: []const u8) void {
    puts("[ \x1B[92mOK\x1B[0m ] ");
    puts(msg);
    putc('\n');
}

pub fn failLog(msg: []const u8) void {
    puts("[\x1B[31mFAIL\x1B[0m] ");
    puts(msg);
    putc('\n');
}

pub fn testOkLog(module: []const u8, test_name: []const u8) void {
    puts("[ \x1B[92mTEST PASS\x1B[0m ] - ");
    puts(module);
    putc(' ');
    puts(test_name);
    putc('\n');
}

pub fn testFailLog(module: []const u8, test_name: []const u8) void {
    puts("[ \x1B[31mTEST FAIL\x1B[0m ] - ");
    puts(module);
    puts(" \t\t");
    puts(test_name);
    putc('\n');
}

pub fn testSkipLog(module: []const u8, test_name: []const u8) void {
    puts("[ \x1B[34mTEST SKIP\x1B[0m ] - ");
    puts(module);
    puts(" \t\t");
    puts(test_name);
    putc('\n');
}

pub fn logHex(val: u64) void {
    putHex(val);
}

/// Emitted once after all modules have been initialized and their tests run.
/// This is the marker the QEMU test harness watches for to know it can stop
/// reading serial output and evaluate pass/fail counts.
pub fn logSummary(passed: u32, failed: u32, skipped: u32) void {
    puts("[SUMMARY] passed=");
    putDec(passed);
    puts(" failed=");
    putDec(failed);
    puts(" skipped=");
    putDec(skipped);
    putc('\n');
}

pub fn bootLogStart() void {
    // Disable UART
    mmioWrite(UART0_BASE + UARTCR, 0);

    // Set baud rate to 115200 (UARTCLK = 24MHz)
    mmioWrite(UART0_BASE + UARTIBRD, 13);
    mmioWrite(UART0_BASE + UARTFBRD, 1);

    // 8 bits, no parity, 1 stop bit, FIFOs enabled
    mmioWrite(UART0_BASE + UARTLCR_H, (1 << 4) | (1 << 5) | (1 << 6));

    // Enable UART, enable transmit & receive
    mmioWrite(UART0_BASE + UARTCR, (1 << 0) | (1 << 8) | (1 << 9));

    okLog("UART serial out initialized");
}
