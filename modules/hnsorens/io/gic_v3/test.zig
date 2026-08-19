//! Tests for the GICv3 driver in `main.zig`, split into their own file
//! (reachable from the build via `main.zig`'s `comptime { _ =
//! @import("test.zig"); }`).
//!
//! gicd_base/gicr_base are QEMU virt's *real* GICv3 addresses
// (`-M virt,gic-version=3`), so these exercise actual hardware, not a
// mock. Several tests don't re-check init_global/init_core's return value
// (only the first, GIC_InitLifecycle, does): g_initialized latches after
// the first successful call, so later calls legitimately return EBUSY --
// this is a deliberately stateful sequence, not independent tests, and
// relies on running in this declaration order (matching the C original).
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const main = @import("main.zig");

const serial_if = main.serial_if;

const GICD_BASE: u64 = 0x8000000;
const GICR_BASE: u64 = 0x80A0000;

fn testInitLifecycle() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.initGlobal(GICD_BASE, GICR_BASE), 0);
    t.expectEqual(@src(), main.initCore(), 0);
    return t.result();
}

fn testConfigureSpi() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    _ = main.initGlobal(GICD_BASE, GICR_BASE);
    _ = main.initCore();

    const uart_irq: u32 = 33;
    t.expectEqual(@src(), main.configure(uart_irq, .edge, 0x80), 0);

    const prio_reg: *volatile u32 = @ptrFromInt(GICD_BASE + 0x0400 + (uart_irq / 4) * 4);
    const shift: u5 = @truncate((uart_irq % 4) * 8);
    t.expectEqual(@src(), (prio_reg.* >> shift) & 0xFF, 0x80);

    const cfg_reg: *volatile u32 = @ptrFromInt(GICD_BASE + 0x0C00 + (uart_irq / 16) * 4);
    const cfg_shift: u5 = @truncate((uart_irq % 16) * 2);
    t.expectEqual(@src(), (cfg_reg.* >> cfg_shift) & 0x3, 0x2);
    return t.result();
}

fn testEnablePpi() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    _ = main.initGlobal(GICD_BASE, GICR_BASE);
    _ = main.initCore();

    const timer_irq: u32 = 27;
    t.expectEqual(@src(), main.enable(timer_irq), 0);

    const isenabler0: *volatile u32 = @ptrFromInt(GICR_BASE + 0x10000 + 0x0100);
    t.expectEqual(@src(), (isenabler0.* >> @as(u5, @truncate(timer_irq))) & 1, 1);
    return t.result();
}

fn testDisablePpi() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    _ = main.initGlobal(GICD_BASE, GICR_BASE);
    _ = main.initCore();

    const test_irq: u32 = 28;
    t.expectEqual(@src(), main.enable(test_irq), 0);

    const isenabler0: *volatile u32 = @ptrFromInt(GICR_BASE + 0x10000 + 0x0100);
    t.expectEqual(@src(), (isenabler0.* >> @as(u5, @truncate(test_irq))) & 1, 1);

    t.expectEqual(@src(), main.disable(test_irq), 0);

    // Disabling a PPI/SGI is signaled through ICENABLER (a distinct
    // write-1-to-clear register), not by directly clearing ISENABLER's
    // bit -- read ISENABLER back to observe the effect.
    t.expectEqual(@src(), (isenabler0.* >> @as(u5, @truncate(test_irq))) & 1, 0);
    return t.result();
}

fn testGroupToggle() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    _ = main.initGlobal(GICD_BASE, GICR_BASE);
    _ = main.initCore();

    const spi: u32 = 40;
    const igroupr: *volatile u32 = @ptrFromInt(GICD_BASE + 0x0080 + (spi / 32) * 4);
    const bit: u5 = @truncate(spi % 32);

    t.expectEqual(@src(), main.setGroup(spi, .non_secure), 0);
    t.expectEqual(@src(), (igroupr.* >> bit) & 1, 1);

    t.expectEqual(@src(), main.setGroup(spi, .secure), 0);
    t.expectEqual(@src(), (igroupr.* >> bit) & 1, 0);
    return t.result();
}

fn testSgiSelf() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    _ = main.initGlobal(GICD_BASE, GICR_BASE);
    _ = main.initCore();

    const gicr_sgi = GICR_BASE + 0x10000;
    const isenabler0: *volatile u32 = @ptrFromInt(gicr_sgi + 0x0100);
    isenabler0.* = 1 << 0;
    const ipriorityr0: *volatile u8 = @ptrFromInt(gicr_sgi + 0x0400);
    ipriorityr0.* = 0x00;

    asm volatile ("msr ICC_PMR_EL1, %[v]"
        :
        : [v] "r" (@as(u64, 0xFF)),
    );
    asm volatile ("msr ICC_IGRPEN1_EL1, %[v]"
        :
        : [v] "r" (@as(u64, 1)),
    );
    asm volatile ("isb");

    const mpidr = main.readMpidr();
    const aff0 = mpidr & 0xFF;
    const aff1 = (mpidr >> 8) & 0xFF;
    const aff2 = (mpidr >> 16) & 0xFF;
    const aff3 = (mpidr >> 32) & 0xFF;

    const sgi_reg = (aff3 << 48) | (aff2 << 32) | (aff1 << 24) |
        ((@as(u64, 0) & 0x0F) << 24) | ((aff0 & 0xF0) << 16) | (@as(u64, 1) << @truncate(aff0 & 0x0F));
    asm volatile ("msr ICC_SGI1R_EL1, %[v]"
        :
        : [v] "r" (sgi_reg),
    );
    asm volatile ("dsb sy");
    asm volatile ("isb");

    const id = main.acknowledge();
    t.expectEqual(@src(), id, 0);
    t.expectEqual(@src(), main.eoi(id), 0);
    return t.result();
}

fn testPriorityMaskFiltersLowerPriority() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    _ = main.initGlobal(GICD_BASE, GICR_BASE);
    _ = main.initCore();

    const gicr_sgi = GICR_BASE + 0x10000;
    const isenabler0: *volatile u32 = @ptrFromInt(gicr_sgi + 0x0100);
    isenabler0.* = 1 << 1; // SGI 1
    const ipriorityr0: *volatile u8 = @ptrFromInt(gicr_sgi + 0x0400 + 1);
    ipriorityr0.* = 0xF0; // low urgency (numerically high priority value)

    asm volatile ("msr ICC_IGRPEN1_EL1, %[v]"
        :
        : [v] "r" (@as(u64, 1)),
    );
    asm volatile ("isb");

    // Numerically *lower* PMR means only *higher*-urgency interrupts get
    // signaled -- 0x10 blocks our 0xF0-priority SGI.
    t.expectEqual(@src(), main.setCorePriorityMask(0x10), 0);

    const mpidr = main.readMpidr();
    const aff0 = mpidr & 0xFF;
    const aff1 = (mpidr >> 8) & 0xFF;
    const aff2 = (mpidr >> 16) & 0xFF;
    const aff3 = (mpidr >> 32) & 0xFF;
    const sgi_reg = (aff3 << 48) | (aff2 << 32) | (aff1 << 24) |
        ((@as(u64, 1) & 0x0F) << 24) | ((aff0 & 0xF0) << 16) | (@as(u64, 1) << @truncate(aff0 & 0x0F));
    asm volatile ("msr ICC_SGI1R_EL1, %[v]"
        :
        : [v] "r" (sgi_reg),
    );
    asm volatile ("dsb sy");
    asm volatile ("isb");

    // Blocked: acknowledge must see nothing (spurious id 1023), not our SGI.
    t.expectEqual(@src(), main.acknowledge(), 1023);

    // Raise the mask back up (allow everything) and the same still-pending
    // SGI should now be visible.
    t.expectEqual(@src(), main.setCorePriorityMask(0xFF), 0);
    const id = main.acknowledge();
    t.expectEqual(@src(), id, 1);
    t.expectEqual(@src(), main.eoi(id), 0);
    return t.result();
}

fn testRegisterHandlerAndBounds() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    _ = main.initGlobal(GICD_BASE, GICR_BASE);
    _ = main.initCore();

    const dummy_ctx: *anyopaque = @ptrFromInt(0xDEADBEEF);
    const dummy_isr: abi.IsrHandler = @ptrFromInt(0x1234);

    t.expectEqual(@src(), main.registerHandler(27, dummy_isr, dummy_ctx), 0);
    t.expectEqual(@src(), main.registerHandler(27, dummy_isr, dummy_ctx), abi.EBUSY);
    t.expectEqual(@src(), main.unregisterHandler(27), 0);

    t.expectEqual(@src(), main.registerHandler(2000, dummy_isr, null), abi.EINVAL);
    t.expectEqual(@src(), main.registerHandler(27, null, null), abi.EINVAL);
    t.expectEqual(@src(), main.unregisterHandler(2000), abi.EINVAL);
    t.expectEqual(@src(), main.setGroup(2000, .non_secure), abi.EINVAL);
    return t.result();
}

fn testRegisterUnregisterRoundtrip() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    _ = main.initGlobal(GICD_BASE, GICR_BASE);
    _ = main.initCore();

    const dummy_isr: abi.IsrHandler = @ptrFromInt(0x2000);
    const vector: u32 = 45;

    t.expectEqual(@src(), main.registerHandler(vector, dummy_isr, null), 0);
    t.expectEqual(@src(), main.unregisterHandler(vector), 0);
    // A vector must become available again immediately after unregister,
    // not stay stuck EBUSY.
    t.expectEqual(@src(), main.registerHandler(vector, dummy_isr, null), 0);
    t.expectEqual(@src(), main.unregisterHandler(vector), 0);
    // Unregistering something not currently registered is not itself an
    // error -- disable() succeeds unconditionally for any in-range vector.
    t.expectEqual(@src(), main.unregisterHandler(vector), 0);
    return t.result();
}

fn testVectorBoundsAcrossOps() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    _ = main.initGlobal(GICD_BASE, GICR_BASE);
    _ = main.initCore();

    const oob: u32 = main.MAX_INTERRUPT_VECTORS;
    t.expectEqual(@src(), main.configure(oob, .edge, 0x80), abi.EINVAL);
    t.expectEqual(@src(), main.enable(oob), abi.EINVAL);
    t.expectEqual(@src(), main.disable(oob), abi.EINVAL);
    t.expectEqual(@src(), main.setGroup(oob, .non_secure), abi.EINVAL);
    t.expectEqual(@src(), main.routeToCore(oob, 0), abi.EINVAL);
    // SPI routing is only valid for vector >= 32 in the first place.
    t.expectEqual(@src(), main.routeToCore(5, 0), abi.EINVAL);
    return t.result();
}

fn testRouteToCore() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    _ = main.initGlobal(GICD_BASE, GICR_BASE);
    _ = main.initCore();

    const spi: u32 = 50;
    const mpidr = main.getCurrentMpidr();
    const expected = mpidr & ~(@as(u64, 1) << 31);

    t.expectEqual(@src(), main.routeToCore(spi, mpidr), 0);

    const router_val: *volatile u64 = @ptrFromInt(GICD_BASE + 0x6000 + @as(u64, spi) * 8);
    t.expectEqual(@src(), router_val.*, expected);

    // SGIs/PPIs (< 32) can't be routed -- routing is an SPI-only concept.
    t.expectEqual(@src(), main.routeToCore(0, mpidr), abi.EINVAL);
    return t.result();
}

var timer_fired: bool = false;

fn timerHandler(arg: ?*anyopaque) callconv(.c) void {
    _ = arg;
    // Disable the timer so it doesn't keep firing.
    asm volatile ("msr cntp_ctl_el0, %[v]"
        :
        : [v] "r" (@as(u64, 0)),
    );
    asm volatile ("isb");
    timer_fired = true;
}

fn testTimerInterrupt() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    _ = main.initGlobal(GICD_BASE, GICR_BASE);
    _ = main.initCore();

    const timer_irq: u32 = 30;
    t.expectEqual(@src(), main.configure(timer_irq, .level, 0x80), 0);
    t.expectEqual(@src(), main.setGroup(timer_irq, .non_secure), 0);
    t.expectEqual(@src(), main.registerHandler(timer_irq, &timerHandler, null), 0);
    _ = main.setCorePriorityMask(0xFF);

    // Program the EL1 physical timer to fire after ~10ms.
    const cntfrq = asm volatile ("mrs %[out], cntfrq_el0"
        : [out] "=r" (-> u64),
    );
    var cval = asm volatile ("mrs %[out], cntpct_el0"
        : [out] "=r" (-> u64),
    );
    cval += cntfrq / 100;
    asm volatile ("msr cntp_cval_el0, %[v]"
        :
        : [v] "r" (cval),
    );
    asm volatile ("msr cntp_ctl_el0, %[v]"
        :
        : [v] "r" (@as(u64, 1)),
    );
    asm volatile ("isb");
    asm volatile ("msr daifclr, #2" ::: .{ .memory = true });

    var timeout: i64 = 1_000_000_000;
    while (!timer_fired and timeout > 0) : (timeout -= 1) {
        asm volatile ("nop");
    }

    asm volatile ("msr daifset, #2" ::: .{ .memory = true });
    asm volatile ("msr cntp_ctl_el0, %[v]"
        :
        : [v] "r" (@as(u64, 0)),
    );

    t.expectEqual(@src(), main.unregisterHandler(timer_irq), 0);
    t.expectTrue(@src(), timer_fired);
    return t.result();
}

comptime {
    abi.kernelTest("init_lifecycle", &testInitLifecycle);
    abi.kernelTest("configure_spi", &testConfigureSpi);
    abi.kernelTest("enable_ppi", &testEnablePpi);
    abi.kernelTest("disable_ppi", &testDisablePpi);
    abi.kernelTest("group_toggle", &testGroupToggle);
    abi.kernelTest("sgi_self", &testSgiSelf);
    abi.kernelTest("priority_mask_filters_lower_priority", &testPriorityMaskFiltersLowerPriority);
    abi.kernelTest("register_handler_and_bounds", &testRegisterHandlerAndBounds);
    abi.kernelTest("register_unregister_roundtrip", &testRegisterUnregisterRoundtrip);
    abi.kernelTest("vector_bounds_across_ops", &testVectorBoundsAcrossOps);
    abi.kernelTest("route_to_core", &testRouteToCore);
    abi.kernelTest("timer_interrupt", &testTimerInterrupt);
}
