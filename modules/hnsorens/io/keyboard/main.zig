//! Raw console-input device, exporting `Keyboard` (category "keyboard").
//!
//! The QEMU virt board has no PS/2 keyboard -- the "keyboard" is the
//! receive side of the same PL011 UART `serial_debug` uses for output.
//! This module does exactly one thing: unmask the UART RX / RX-timeout
//! interrupt, register an ISR with the interrupt manager (UART is SPI 1
//! -> GIC INTID 33), and hand every received byte, raw, to a registered
//! listener. Line discipline (echo, editing, canonical mode, blocking
//! reads) lives in `hnsorens.io.tty`; file-descriptor plumbing lives in
//! `hnsorens.fs.fd`.
//!
//! When no listener is registered, bytes fall into a small ring the
//! `read`/`available` calls drain -- enough for polling callers and for
//! this module's own tests to run without the tty layer.
const abi = @import("abi");
const kernel_fmt = @import("kernel_fmt");
const mmio = @import("mmio");
const spinlock = @import("spinlock");

pub const gic_if = abi.importInterface(abi.InterruptManager);
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
var s_listener: ?abi.KeyListener = null;

fn ringPush(b: u8) void {
    s_lock.lock();
    if (s_count < RING_SIZE) {
        s_ring[(s_head + s_count) % RING_SIZE] = b;
        s_count += 1;
    }
    s_lock.unlock();
}

/// One received byte: hand it to the listener, or buffer it if none.
/// Called from the ISR and directly from tests.
pub fn feedByte(b: u8) void {
    if (s_listener) |l| {
        l(b);
    } else {
        ringPush(b);
    }
}

fn uartIsr(ctx: ?*anyopaque) callconv(.c) void {
    _ = ctx;
    while ((Fr.read(UART0_BASE) & FR_RXFE) == 0) {
        feedByte(@truncate(Dr.read(UART0_BASE) & 0xFF));
    }
    Icr.write(UART0_BASE, ICR_RX);
}

// --- exported vtable ------------------------------------------

pub fn setListener(cb: ?abi.KeyListener) callconv(.c) void {
    s_listener = cb;
}

pub fn read(buf: [*]u8, max: u64) callconv(.c) u64 {
    s_lock.lock();
    defer s_lock.unlock();
    var i: u64 = 0;
    while (i < max and s_count > 0) : (i += 1) {
        buf[i] = s_ring[s_head];
        s_head = (s_head + 1) % RING_SIZE;
        s_count -= 1;
    }
    return i;
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

    kernel_fmt.print(serial_if, "[keyboard] PL011 RX ready on IRQ {d}\n", .{UART_IRQ});
}

comptime {
    abi.exportInterface("pl011_rx", abi.Keyboard, .{
        .set_listener = setListener,
        .read = read,
        .available = available,
    });
}

comptime {
    _ = @import("test.zig");
}
