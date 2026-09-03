//! Console line discipline, exporting `Tty` (category "tty"). Sits on
//! `keyboard`'s raw byte stream (registers itself as the listener) and
//! `serial`'s output.
//!
//! Canonical mode (the default): keystrokes accumulate in a line buffer
//! with editing -- backspace, ^U (kill line), ^W (kill word) -- and are
//! echoed as typed; CR is translated to LF; a `read` only completes once
//! Enter commits a whole line (or ^D delivers a partial line / EOF). Raw
//! mode: every byte is delivered as it arrives, echo optional.
//!
//! Owns the block/wake of a process waiting on console input: an empty
//! read `scheduler.block()`s the caller and the keyboard ISR path
//! (`ttyRxByte`) `scheduler.wake()`s it once a line is ready. This is the
//! standard tty layering HendOS's `vcon.c` did in one lump.
const abi = @import("abi");
const kernel_fmt = @import("kernel_fmt");
const spinlock = @import("spinlock");

pub const keyboard_if = abi.importInterface(abi.Keyboard);
pub const sched_if = abi.importInterface(abi.Scheduler);
pub const serial_if = abi.importInterface(abi.Serial);
pub const signal_if = abi.importInterface(abi.Signal);
pub const process_if = abi.importInterface(abi.Process); // block/wake test only

const LINE_MAX = 256;
const COOKED_SIZE = 1024;

const BS: u8 = 0x08;
const DEL: u8 = 0x7F;
const CTRL_C: u8 = 0x03;
const CTRL_BACKSLASH: u8 = 0x1C;
const CTRL_U: u8 = 0x15;
const CTRL_W: u8 = 0x17;
const CTRL_D: u8 = 0x04;

var s_canonical: bool = true;
var s_echo: bool = true;

var s_line: [LINE_MAX]u8 = undefined;
var s_line_len: usize = 0;

var s_cooked: [COOKED_SIZE]u8 = undefined;
var s_ck_head: usize = 0;
var s_ck_count: usize = 0;
var s_eof: bool = false;

var s_blocked_reader: u32 = 0;
var s_lock: spinlock.SpinLock = .{};

// --- output helpers -------------------------------------------

fn out(bytes: []const u8) void {
    _ = serial_if.write(bytes.ptr, bytes.len);
}

fn outByte(b: u8) void {
    var c = b;
    _ = serial_if.write(@as([*]const u8, @ptrCast(&c)), 1);
}

fn eraseOne() void {
    out("\x08 \x08");
}

// --- cooked ring (caller holds s_lock) -----------------------

fn cookedPushLocked(b: u8) void {
    if (s_ck_count < COOKED_SIZE) {
        s_cooked[(s_ck_head + s_ck_count) % COOKED_SIZE] = b;
        s_ck_count += 1;
    }
}

fn wakeReaderLocked() void {
    const p = s_blocked_reader;
    if (p != 0) {
        s_blocked_reader = 0;
        _ = sched_if.wake(p);
    }
}

fn commitLineLocked(with_newline: bool) void {
    for (s_line[0..s_line_len]) |c| cookedPushLocked(c);
    if (with_newline) cookedPushLocked('\n');
    s_line_len = 0;
    wakeReaderLocked();
}

// --- the keyboard listener (runs in ISR context) ------------

pub fn ttyRxByte(byte: u8) callconv(.c) void {
    s_lock.lock();
    defer s_lock.unlock();

    const b: u8 = if (byte == '\r') '\n' else byte;

    if (!s_canonical) {
        cookedPushLocked(b);
        if (s_echo) outByte(byte);
        wakeReaderLocked();
        return;
    }

    switch (b) {
        CTRL_C, CTRL_BACKSLASH => {
            // No foreground process group yet: signal whatever user
            // process is currently running. Discard the pending line.
            if (s_echo) out("^C\r\n");
            s_line_len = 0;
            const cur = sched_if.current();
            if (cur != 0) signal_if.raise(cur, if (b == CTRL_C) 2 else 3); // SIGINT / SIGQUIT
        },
        '\n' => {
            if (s_echo) out("\r\n");
            commitLineLocked(true);
        },
        BS, DEL => {
            if (s_line_len > 0) {
                s_line_len -= 1;
                if (s_echo) eraseOne();
            }
        },
        CTRL_U => {
            while (s_line_len > 0) : (s_line_len -= 1) {
                if (s_echo) eraseOne();
            }
        },
        CTRL_W => {
            while (s_line_len > 0 and s_line[s_line_len - 1] == ' ') : (s_line_len -= 1) {
                if (s_echo) eraseOne();
            }
            while (s_line_len > 0 and s_line[s_line_len - 1] != ' ') : (s_line_len -= 1) {
                if (s_echo) eraseOne();
            }
        },
        CTRL_D => {
            if (s_line_len == 0) {
                s_eof = true;
                wakeReaderLocked();
            } else {
                commitLineLocked(false); // deliver the partial line now
            }
        },
        else => {
            const printable = (b >= 0x20 and b < 0x7F) or b == '\t';
            if (printable and s_line_len < LINE_MAX - 1) {
                s_line[s_line_len] = b;
                s_line_len += 1;
                if (s_echo) outByte(b);
            }
        },
    }
}

// --- exported vtable ---------------------------------------

pub fn read(buf: [*]u8, max: u64) callconv(.c) u64 {
    while (true) {
        s_lock.lock();
        if (s_ck_count > 0) {
            var i: u64 = 0;
            while (i < max and s_ck_count > 0) : (i += 1) {
                buf[i] = s_cooked[s_ck_head];
                s_ck_head = (s_ck_head + 1) % COOKED_SIZE;
                s_ck_count -= 1;
            }
            s_lock.unlock();
            return i;
        }
        if (s_eof) {
            s_eof = false;
            s_lock.unlock();
            return 0;
        }
        const me = sched_if.current();
        if (me == 0) {
            s_lock.unlock();
            return 0;
        }
        s_blocked_reader = me;
        s_lock.unlock();
        _ = sched_if.block();
    }
}

pub fn write(buf: [*]const u8, len: u64) callconv(.c) u64 {
    _ = serial_if.write(buf, len);
    return len;
}

pub fn setMode(canonical: bool, echo: bool) callconv(.c) void {
    s_lock.lock();
    defer s_lock.unlock();
    s_canonical = canonical;
    s_echo = echo;
    s_line_len = 0; // discard any half-typed line on a mode change
}

/// Test-only: clear all discipline state back to defaults (canonical,
/// echo off), so each kernelTest starts clean.
pub fn testReset() void {
    s_lock.lock();
    defer s_lock.unlock();
    s_canonical = true;
    s_echo = false;
    s_line_len = 0;
    s_ck_head = 0;
    s_ck_count = 0;
    s_eof = false;
    s_blocked_reader = 0;
}

/// Test-only: whether an EOF is queued (set by ^D on an empty line,
/// cleared by the next `read`).
pub fn testEofPending() bool {
    return s_eof;
}

pub fn main(boot_info_ptr: *anyopaque) void {
    _ = boot_info_ptr;
    keyboard_if.set_listener(&ttyRxByte);
    kernel_fmt.print(serial_if, "[tty] line discipline attached to keyboard\n", .{});
}

comptime {
    abi.exportInterface("console", abi.Tty, .{
        .read = read,
        .write = write,
        .set_mode = setMode,
    });
}

comptime {
    _ = @import("test.zig");
}
