//! kernelTests for the EL1 exception dispatcher. These fire *real*
//! synchronous exceptions on the live CPU -- `svc` instructions and a
//! load from a deliberately unmapped high address -- and assert the
//! registered callback runs with a correct `TrapFrame`, that mutating the
//! frame takes effect on `eret` (stepping past the faulting instruction,
//! returning a value in x0), and that the default policy keeps the core
//! alive when no callback is registered.
//!
//! Every test registers a callback that advances `elr` past the trapping
//! instruction (or relies on the default policy doing so for `svc`), so
//! none of them can loop forever on a re-executed fault.
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const main = @import("main.zig");

const serial_if = main.serial_if;

/// A canonical low (TTBR0) virtual address well past the end of the
/// bootloader's identity map (~tens of GiB) and nowhere near any device
/// window -> a clean translation fault (data abort) with FAR_EL1 equal to
/// this exact value. Chosen with a zero top byte so TBI, if enabled,
/// can't perturb the reported address.
const UNMAPPED_ADDR: u64 = 0x0000080000000000; // 8 TiB

fn ec(esr: u64) u64 {
    return (esr >> 26) & 0x3F;
}

// --- shared handler state ------------------------------------------

var g_hits: u32 = 0;
var g_esr: u64 = 0;
var g_far: u64 = 0;
var g_origin: abi.ExceptionOrigin = .current_el_sp0;
var g_arg_seen: usize = 0;

fn resetState() void {
    g_hits = 0;
    g_esr = 0;
    g_far = 0;
    g_origin = .current_el_sp0;
    g_arg_seen = 0;
}

/// Records frame details. For an abort it steps `elr` past the faulting
/// instruction; for an SVC the CPU already advanced `elr`, so it leaves it
/// alone.
fn recordingHandler(frame: *abi.TrapFrame, origin: abi.ExceptionOrigin, arg: ?*anyopaque) callconv(.c) abi.ExceptionOutcome {
    g_hits += 1;
    g_esr = frame.esr;
    g_far = frame.far;
    g_origin = origin;
    g_arg_seen = @intFromPtr(arg);
    const ecv = ec(frame.esr);
    if (ecv == 0x24 or ecv == 0x25 or ecv == 0x20 or ecv == 0x21) frame.elr += 4;
    return .handled;
}

/// Never actually invoked in the tests that use it.
fn noopHandler(frame: *abi.TrapFrame, origin: abi.ExceptionOrigin, arg: ?*anyopaque) callconv(.c) abi.ExceptionOutcome {
    _ = frame;
    _ = origin;
    _ = arg;
    return .handled;
}

// --- 1. an EL1 SVC reaches the .sync_svc callback -----------------

fn testSvcDispatches() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    resetState();
    t.expectEqual(@src(), main.registerHandler(.sync_svc, &recordingHandler, null), 0);

    asm volatile ("svc #0" ::: .{ .memory = true });
    t.expectEqual(@src(), g_hits, 1);
    t.expectEqual(@src(), ec(g_esr), 0x15); // EC == SVC from AArch64
    t.expectEqual(@src(), g_origin, abi.ExceptionOrigin.current_el_spx);

    asm volatile ("svc #0" ::: .{ .memory = true });
    asm volatile ("svc #0" ::: .{ .memory = true });
    t.expectEqual(@src(), g_hits, 3);

    t.expectEqual(@src(), main.unregisterHandler(.sync_svc), 0);
    return t.result();
}

// --- 2. the SVC immediate lands in ESR.ISS -----------------------

var g_iss: u64 = 0;

fn issHandler(frame: *abi.TrapFrame, origin: abi.ExceptionOrigin, arg: ?*anyopaque) callconv(.c) abi.ExceptionOutcome {
    _ = origin;
    _ = arg;
    g_iss = frame.esr & 0xFFFF;
    return .handled;
}

fn testSvcImmediateInEsr() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    g_iss = 0;
    t.expectEqual(@src(), main.registerHandler(.sync_svc, &issHandler, null), 0);

    asm volatile ("svc #0x42" ::: .{ .memory = true });
    t.expectEqual(@src(), g_iss, 0x42);

    t.expectEqual(@src(), main.unregisterHandler(.sync_svc), 0);
    return t.result();
}

// --- 3. a data abort is caught and recovered from ---------------

fn testDataAbortRecovered() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    resetState();
    t.expectEqual(@src(), main.registerHandler(.sync_data_abort, &recordingHandler, null), 0);

    // Exactly one instruction, so elr += 4 in the handler lands on the
    // next one. x9 receives garbage (whatever the fault left) -- unused.
    asm volatile ("ldr x9, [%[addr]]"
        :
        : [addr] "r" (UNMAPPED_ADDR),
        : .{ .x9 = true, .memory = true });

    t.expectEqual(@src(), g_hits, 1);
    t.expectEqual(@src(), g_far, UNMAPPED_ADDR);
    // Same-EL data abort => EC 0x24 or 0x25.
    t.expectTrue(@src(), ec(g_esr) == 0x24 or ec(g_esr) == 0x25);
    t.expectEqual(@src(), g_origin, abi.ExceptionOrigin.current_el_spx);

    t.expectEqual(@src(), main.unregisterHandler(.sync_data_abort), 0);
    return t.result();
}

// --- 4. an unregistered SVC resumes gracefully (default policy) ----
//
// The CPU sets ELR past the `svc` on entry, so the default policy just
// logs and returns -- execution must continue at the next instruction,
// not loop or die.

fn testUnregisteredSvcResumesGracefully() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    _ = main.unregisterHandler(.sync_svc); // ensure nothing is registered

    var reached = false;
    asm volatile ("svc #0" ::: .{ .memory = true });
    reached = true;

    t.expectTrue(@src(), reached);
    return t.result();
}

// --- 5. syscall-style: number in x8, return value written to x0 ---

fn syscallReturnHandler(frame: *abi.TrapFrame, origin: abi.ExceptionOrigin, arg: ?*anyopaque) callconv(.c) abi.ExceptionOutcome {
    _ = origin;
    _ = arg;
    const nr = frame.x[8];
    frame.x[0] = nr *% 3 + 1;
    return .handled;
}

fn testSyscallReturnValueViaX0() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.registerHandler(.sync_svc, &syscallReturnHandler, null), 0);

    const result = asm volatile (
        \\ mov x8, #14
        \\ svc #0
        : [ret] "={x0}" (-> u64),
        :
        : .{ .x8 = true, .memory = true });

    t.expectEqual(@src(), result, 14 * 3 + 1);

    t.expectEqual(@src(), main.unregisterHandler(.sync_svc), 0);
    return t.result();
}

// --- 6. registration bookkeeping -------------------------------

fn testRegisterRejectsDuplicateAndNull() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    t.expectEqual(@src(), main.registerHandler(.sync_svc, &noopHandler, null), 0);
    t.expectEqual(@src(), main.registerHandler(.sync_svc, &noopHandler, null), abi.EBUSY);
    t.expectEqual(@src(), main.unregisterHandler(.sync_svc), 0);

    t.expectEqual(@src(), main.registerHandler(.sync_svc, null, null), abi.EINVAL);
    // Unregister is idempotent -- removing an absent handler is not an error.
    t.expectEqual(@src(), main.unregisterHandler(.sync_svc), 0);

    // The slot must be reusable immediately after unregister.
    t.expectEqual(@src(), main.registerHandler(.sync_svc, &noopHandler, null), 0);
    t.expectEqual(@src(), main.unregisterHandler(.sync_svc), 0);
    return t.result();
}

// --- 7. the arg pointer round-trips to the callback -----------

fn testArgPassedThrough() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    resetState();
    const token: *anyopaque = @ptrFromInt(0xABCD1234);
    t.expectEqual(@src(), main.registerHandler(.sync_svc, &recordingHandler, token), 0);

    asm volatile ("svc #0" ::: .{ .memory = true });
    t.expectEqual(@src(), g_arg_seen, 0xABCD1234);

    t.expectEqual(@src(), main.unregisterHandler(.sync_svc), 0);
    return t.result();
}

// --- 8. a callback that returns .unhandled falls through to default

var g_unhandled_hits: u32 = 0;

fn decliningHandler(frame: *abi.TrapFrame, origin: abi.ExceptionOrigin, arg: ?*anyopaque) callconv(.c) abi.ExceptionOutcome {
    _ = frame;
    _ = origin;
    _ = arg;
    g_unhandled_hits += 1;
    return .unhandled; // deliberately does NOT step elr
}

fn testUnhandledFallsThroughToDefault() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    g_unhandled_hits = 0;
    t.expectEqual(@src(), main.registerHandler(.sync_svc, &decliningHandler, null), 0);

    var reached = false;
    asm volatile ("svc #0" ::: .{ .memory = true });
    reached = true;

    t.expectEqual(@src(), g_unhandled_hits, 1); // callback ran
    t.expectTrue(@src(), reached); // default policy still stepped past the svc

    t.expectEqual(@src(), main.unregisterHandler(.sync_svc), 0);
    return t.result();
}

comptime {
    abi.kernelTest("svc_dispatches", &testSvcDispatches);
    abi.kernelTest("svc_immediate_in_esr", &testSvcImmediateInEsr);
    abi.kernelTest("data_abort_recovered", &testDataAbortRecovered);
    abi.kernelTest("unregistered_svc_resumes_gracefully", &testUnregisteredSvcResumesGracefully);
    abi.kernelTest("syscall_return_value_via_x0", &testSyscallReturnValueViaX0);
    abi.kernelTest("register_rejects_duplicate_and_null", &testRegisterRejectsDuplicateAndNull);
    abi.kernelTest("arg_passed_through", &testArgPassedThrough);
    abi.kernelTest("unhandled_falls_through_to_default", &testUnhandledFallsThroughToDefault);
}
