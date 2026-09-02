//! Console keyboard input, exporting `Keyboard` (category "keyboard").
//!
//! The QEMU virt board has no PS/2 keyboard -- the "keyboard" is the
//! receive side of the same PL011 UART `serial_debug` uses for output.
//! This module unmasks the UART RX / RX-timeout interrupt, registers an
//! ISR with the interrupt manager (UART is SPI 1 -> GIC INTID 33), and
//! buffers every received byte into a ring, echoing each keystroke so
//! typing shows up. It also owns `SYS_read` on fd 0: an empty read blocks
//! the calling process and the ISR wakes it when a byte arrives.
//!
//! HendOS had a real `keyboard.c` (PS/2 scancodes); this is the virt-board
//! equivalent, much simpler because the UART already delivers ASCII.
const abi = @import("abi");
const kernel_fmt = @import("kernel_fmt");
const mmio = @import("mmio");
const spinlock = @import("spinlock");

pub const gic_if = abi.importInterface(abi.InterruptManager);
pub const sched_if = abi.importInterface(abi.Scheduler);
pub const sc_if = abi.importInterface(abi.Syscalls);
pub const process_if = abi.importInterface(abi.Process); // for the block/wake test
pub const serial_if = abi.importInterface(abi.Serial);

const UART0_BASE: u64 = 0x09000000;
const UART_IRQ: u32 = 33; // SPI 1

const Dr = mmio.Reg(u32, 0x00);
const Fr = mmio.Reg(u32, 0x18);
const LcrH = mmio.Reg(u32, 0x2C);
const Cr = mmio.Reg(u32, 0x30);
const Imsc = mmio.Reg(u32, 0x38);
const Icr = mmio.Reg(u32, 0x44);

const FR_RXFE: u32 = 1 << 4; // RX FIFO empty
const CR_UARTEN: u32 = 1 << 0;
const CR_TXE: u32 = 1 << 8;
const CR_RXE: u32 = 1 << 9;
const LCRH_FEN: u32 = 1 << 4; // FIFO enable
const IMSC_RXIM: u32 = 1 << 4; // RX interrupt
const IMSC_RTIM: u32 = 1 << 6; // RX timeout interrupt
const ICR_RX: u32 = (1 << 4) | (1 << 6);

const RING_SIZE = 256;
var s_ring: [RING_SIZE]u8 = undefined;
var s_head: usize = 0;
var s_count: usize = 0;
var s_lock: spinlock.SpinLock = .{};
var s_blocked_reader: u32 = 0;

// --- ring buffer -------------------------------------------------

fn ringPush(b: u8) void {
    s_lock.lock();
    if (s_count < RING_SIZE) {
        s_ring[(s_head + s_count) % RING_SIZE] = b;
        s_count += 1;
    }
    s_lock.unlock();
}

fn ringPop(buf: [*]u8, max: u64) u64 {
    s_lock.lock();
    var i: u64 = 0;
    while (i < max and s_count > 0) : (i += 1) {
        buf[i] = s_ring[s_head];
        s_head = (s_head + 1) % RING_SIZE;
        s_count -= 1;
    }
    s_lock.unlock();
    return i;
}

/// Handle one received byte: translate CR->LF, echo it, buffer it, and
/// wake a blocked reader. Called from the ISR (and directly from tests).
pub fn feedByte(raw: u8) void {
    const b: u8 = if (raw == '\r') '\n' else raw;

    if (b == 0x7F or b == 0x08) {
        const seq = "\x08 \x08";
        _ = serial_if.write(seq, seq.len);
    } else if (b == '\n') {
        const seq = "\r\n";
        _ = serial_if.write(seq, seq.len);
    } else {
        var c = b;
        _ = serial_if.write(@as([*]const u8, @ptrCast(&c)), 1);
    }
    ringPush(b);

    const p = s_blocked_reader;
    if (p != 0) {
        s_blocked_reader = 0;
        _ = sched_if.wake(p);
    }
}

/// Test-only: buffer a raw byte with no translation, echo, or wake.
pub fn testPush(b: u8) void {
    ringPush(b);
}

fn uartIsr(ctx: ?*anyopaque) callconv(.c) void {
    _ = ctx;
    while ((Fr.read(UART0_BASE) & FR_RXFE) == 0) {
        feedByte(@truncate(Dr.read(UART0_BASE) & 0xFF));
    }
    Icr.write(UART0_BASE, ICR_RX);
}

// --- blocking read (SYS_read) ----------------------------------

pub fn readBlocking(buf: [*]u8, max: u64) u64 {
    while (true) {
        const n = ringPop(buf, max);
        if (n > 0) return n;
        const me = sched_if.current();
        if (me == 0) return 0; // no scheduled context to block
        s_blocked_reader = me;
        // In the real path this runs inside the SVC handler with IRQs
        // masked, so the ISR can't push+wake between the empty check
        // above and this block(); block() switches to the idle loop,
        // which re-enables IRQs before WFI.
        _ = sched_if.block();
    }
}

fn sysRead(args: *const abi.SyscallArgs, ctx: ?*anyopaque) callconv(.c) i64 {
    _ = ctx;
    const fd = args.arg[0];
    const buf = args.arg[1];
    const count = args.arg[2];
    if (fd != 0) return -@as(i64, abi.EBADF);
    if (count == 0) return 0;
    return @intCast(readBlocking(@ptrFromInt(buf), count));
}

// --- exported vtable ------------------------------------------

pub fn read(buf: [*]u8, max: u64) callconv(.c) u64 {
    return ringPop(buf, max);
}

pub fn available() callconv(.c) u64 {
    s_lock.lock();
    defer s_lock.unlock();
    return s_count;
}

fn readMpidr() u64 {
    return asm volatile ("mrs %[v], mpidr_el1"
        : [v] "=r" (-> u64),
    );
}

pub fn main(boot_info_ptr: *anyopaque) void {
    _ = boot_info_ptr;

    Cr.setBits(UART0_BASE, CR_UARTEN | CR_TXE | CR_RXE);
    LcrH.setBits(UART0_BASE, LCRH_FEN);
    Icr.write(UART0_BASE, ICR_RX);
    Imsc.setBits(UART0_BASE, IMSC_RXIM | IMSC_RTIM);

    _ = gic_if.configure(UART_IRQ, .level, 0x80);
    _ = gic_if.set_group(UART_IRQ, .non_secure);
    _ = gic_if.route_to_core(UART_IRQ, readMpidr());
    _ = gic_if.set_core_priority_mask(0xFF);
    _ = gic_if.register_handler(UART_IRQ, &uartIsr, null);

    _ = sc_if.register(abi.SYS_read, &sysRead, null);

    kernel_fmt.print(serial_if, "[keyboard] PL011 RX ready on IRQ {d}\n", .{UART_IRQ});
}

comptime {
    abi.exportInterface("pl011_rx", abi.Keyboard, .{
        .read = read,
        .available = available,
    });
}

comptime {
    _ = @import("test.zig");
}
