//! GICv3 interrupt controller driver, exporting `InterruptManager`
//! (category "interrupt_manager"). Supports multi-core (GICv3
//! redistributors), SPI routing, and SGI/PPI configuration.
const std = @import("std");
const abi = @import("abi");
const kernel_fmt = @import("kernel_fmt");
const kernel_test = @import("kernel_test");
const mmio = @import("mmio");
const sysreg = @import("sysreg");
const spinlock = @import("spinlock");

pub const serial_if = abi.importInterface(abi.Serial);

pub const MAX_INTERRUPT_VECTORS = 1024;
const MAX_CORES_SUPPORTED = 64;

const RegisteredIsr = struct {
    handler: ?abi.IsrHandler = null,
    arg: ?*anyopaque = null,
};

const CoreMap = struct {
    allocated: bool = false,
    mpidr: u64 = 0,
};

var g_gicd: u64 = 0; // Distributor physical base
var g_gicr: u64 = 0; // Redistributor physical base (first frame)
var g_initialized: bool = false;

var s_isr_table: [MAX_INTERRUPT_VECTORS]RegisteredIsr = [_]RegisteredIsr{.{}} ** MAX_INTERRUPT_VECTORS;
var s_core_topology: [MAX_CORES_SUPPORTED]CoreMap = [_]CoreMap{.{}} ** MAX_CORES_SUPPORTED;
var s_isr_lock: spinlock.SpinLock = .{};

// --- Exception vector table -------------------------------------------
//
// AArch64 requires a 2KB-aligned table of 16 128-byte-spaced entries (4
// exception classes x 4 source configurations); every entry lands on the
// same trampoline, which saves all GPRs, calls into `c_interrupt_handler`
// below, restores them, and returns. `save_context`/`restore_context` are
// plain GNU-as macros, valid inside a Zig `asm` block same as anywhere
// else -- this is a near-verbatim port of the C driver's vector.S.
comptime {
    asm (
        \\.balign 2048
        \\.global exception_vector_table
        \\exception_vector_table:
        \\    /* --- Current EL with SP0 (Offsets 0x000 - 0x180) --- */
        \\    .balign 128
        \\    b irq_vector_entry // Synchronous
        \\    .balign 128
        \\    b irq_vector_entry // IRQ
        \\    .balign 128
        \\    b irq_vector_entry // FIQ
        \\    .balign 128
        \\    b irq_vector_entry // SError
        \\
        \\    /* --- Current EL with SPx (Offsets 0x200 - 0x380) --- */
        \\    .balign 128
        \\    b irq_vector_entry // Synchronous
        \\    .balign 128
        \\    b irq_vector_entry // IRQ (this is where the timer interrupt lands)
        \\    .balign 128
        \\    b irq_vector_entry // FIQ
        \\    .balign 128
        \\    b irq_vector_entry // SError
        \\
        \\.macro save_context
        \\    sub sp, sp, #256
        \\    stp x0, x1, [sp, #0]
        \\    stp x2, x3, [sp, #16]
        \\    stp x4, x5, [sp, #32]
        \\    stp x6, x7, [sp, #48]
        \\    stp x8, x9, [sp, #64]
        \\    stp x10, x11, [sp, #80]
        \\    stp x12, x13, [sp, #96]
        \\    stp x14, x15, [sp, #112]
        \\    stp x16, x17, [sp, #128]
        \\    stp x18, x19, [sp, #144]
        \\    stp x20, x21, [sp, #160]
        \\    stp x22, x23, [sp, #176]
        \\    stp x24, x25, [sp, #192]
        \\    stp x26, x27, [sp, #208]
        \\    stp x28, x29, [sp, #224]
        \\    str x30, [sp, #240]
        \\.endm
        \\
        \\.macro restore_context
        \\    ldp x0, x1, [sp, #0]
        \\    ldp x2, x3, [sp, #16]
        \\    ldp x4, x5, [sp, #32]
        \\    ldp x6, x7, [sp, #48]
        \\    ldp x8, x9, [sp, #64]
        \\    ldp x10, x11, [sp, #80]
        \\    ldp x12, x13, [sp, #96]
        \\    ldp x14, x15, [sp, #112]
        \\    ldp x16, x17, [sp, #128]
        \\    ldp x18, x19, [sp, #144]
        \\    ldp x20, x21, [sp, #160]
        \\    ldp x22, x23, [sp, #176]
        \\    ldp x24, x25, [sp, #192]
        \\    ldp x26, x27, [sp, #208]
        \\    ldp x28, x29, [sp, #224]
        \\    ldr x30, [sp, #240]
        \\    add sp, sp, #256
        \\.endm
        \\
        \\.global irq_vector_entry
        \\irq_vector_entry:
        \\    save_context
        \\    bl c_interrupt_handler
        \\    restore_context
        \\    eret
    );
}

extern var exception_vector_table: u8;

// --- GICD (Distributor) register map -------------------------------------
//
// Each interrupt-indexed register below is defined once via
// `mmio.IndexedField`, which owns the "N elements packed per register"
// arithmetic (e.g. 4 one-byte priorities per 32-bit IPRIORITYR word) that
// every accessor used to redo by hand. `Isenabler`/`Icenabler` are
// write-1-to-set/write-1-to-clear registers -- a 0 bit there means "leave
// alone", not "clear" -- so they're driven through `writeOneHot` (a raw
// one-hot write) rather than `write` (a read-modify-write), matching their
// hardware semantics.

const GicdCtlr = packed struct(u32) {
    _reserved0: u1 = 0,
    /// GICD_CTLR.EnableGrp1NS (bit 1): enable Group 1 Non-secure interrupts.
    ns_ena_grp1ns: bool = false,
    _reserved1: u2 = 0,
    /// GICD_CTLR.ARE_NS (bit 4): enable affinity-routed (GICv3-style) SPIs.
    ns_are_ns: bool = false,
    _reserved2: u27 = 0,
};

const Gicd = struct {
    const Ctlr = mmio.Reg(GicdCtlr, 0x0000);
    const Igroupr = mmio.IndexedField(bool, 0x0080, u32);
    const Isenabler = mmio.IndexedField(bool, 0x0100, u32);
    const Icenabler = mmio.IndexedField(bool, 0x0180, u32);
    const Ipriorityr = mmio.IndexedField(u8, 0x0400, u32);
    const Icfgr = mmio.IndexedField(u2, 0x0C00, u32);
    const Igrpmodr = mmio.IndexedField(bool, 0x0D00, u32);
    const Irouter = mmio.RegArray(u64, 0x6000, 8);
};

// --- GICR (Redistributor) -- per-core frame offsets ---------------------

const GicrWaker = packed struct(u32) {
    _reserved0: u1 = 0,
    processor_sleep: bool = false,
    children_asleep: bool = false,
    _reserved1: u29 = 0,
};

// SGI/PPI-indexed registers (Isenabler..Igrpmodr) live in the
// redistributor's second 64KB frame -- GICR_SGI_OFFSET is folded into
// their offsets here so callers pass the same per-core base as `Waker`.
const GICR_SGI_OFFSET: u64 = 0x10000;

const Gicr = struct {
    const Waker = mmio.Reg(GicrWaker, 0x0014);
    const Igroupr = mmio.IndexedField(bool, GICR_SGI_OFFSET + 0x0080, u32);
    const Isenabler = mmio.IndexedField(bool, GICR_SGI_OFFSET + 0x0100, u32);
    const Icenabler = mmio.IndexedField(bool, GICR_SGI_OFFSET + 0x0180, u32);
    const Ipriorityr = mmio.IndexedField(u8, GICR_SGI_OFFSET + 0x0400, u32);
    const Icfgr = mmio.IndexedField(u2, GICR_SGI_OFFSET + 0x0C00, u32);
    const Igrpmodr = mmio.IndexedField(bool, GICR_SGI_OFFSET + 0x0D00, u32);
};

// --- CPU interface system registers -------------------------------------

const IccSre = packed struct(u64) {
    /// ICC_SRE_EL1.SRE (bit 0): system register interface enabled.
    sre: bool = false,
    /// ICC_SRE_EL1.DFB (bit 1): disable IRQ/FIQ bypass.
    dfb: bool = false,
    /// ICC_SRE_EL1.DIB (bit 2): disable IRQ/FIQ bypass (the other one).
    dib: bool = false,
    _reserved: u61 = 0,
};

const IccIgrpen1 = packed struct(u64) {
    /// ICC_IGRPEN1_EL1.Enable (bit 0): enable Group 1 interrupts.
    enable: bool = false,
    _reserved: u63 = 0,
};

const Icc = struct {
    const Sre = sysreg.Reg(IccSre, "ICC_SRE_EL1");
    const Pmr = sysreg.Reg(u64, "ICC_PMR_EL1");
    const Bpr1 = sysreg.Reg(u64, "ICC_BPR1_EL1");
    const Ctlr = sysreg.Reg(u64, "ICC_CTLR_EL1");
    const Igrpen1 = sysreg.Reg(IccIgrpen1, "ICC_IGRPEN1_EL1");
    const Iar1 = sysreg.Reg(u64, "ICC_IAR1_EL1");
    const Eoir1 = sysreg.Reg(u64, "ICC_EOIR1_EL1");
};

const Mpidr = sysreg.Reg(u64, "mpidr_el1");
const Vbar = sysreg.Reg(u64, "vbar_el1");

fn mpidrToCoreId(mpidr: u64) u32 {
    const aff0: u32 = @truncate(mpidr & 0xFF);
    const aff1: u32 = @truncate((mpidr >> 8) & 0xFF);
    return aff0 + aff1; // single cluster => linear index
}

pub fn readMpidr() u64 {
    return Mpidr.read();
}

fn getCurrentGicrBase() u64 {
    const cid = mpidrToCoreId(readMpidr());
    return g_gicr + @as(u64, cid) * 0x20000;
}

fn gicv3InitRedistributor() c_int {
    const base = getCurrentGicrBase();
    Gicr.Waker.modify(base, .{ .processor_sleep = false });
    asm volatile ("dsb sy");

    var i: u32 = 0;
    while (i < 1000) : (i += 1) {
        if (!Gicr.Waker.read(base).children_asleep) return 0;
    }
    return abi.EIO;
}

fn gicv3InitCpuInterfaceNs() c_int {
    Icc.Sre.modify(.{ .sre = true, .dfb = true, .dib = true });
    asm volatile ("isb");

    Icc.Pmr.write(0xFF);
    Icc.Bpr1.write(0);
    Icc.Ctlr.write(0);
    Icc.Igrpen1.write(.{ .enable = true });
    asm volatile ("dsb sy");
    asm volatile ("isb");
    return 0;
}

fn iccWritePmrEl1(val: u64) void {
    Icc.Pmr.write(val);
    asm volatile ("isb");
}

fn iccReadIar1El1() u64 {
    return Icc.Iar1.read();
}

fn iccWriteEoir1El1(val: u64) void {
    Icc.Eoir1.write(val);
    asm volatile ("isb");
}

// --- 1. System-wide initialization --------------------------------------

pub fn initGlobal(d_base: u64, r_base: u64) callconv(.c) c_int {
    if (d_base == 0 or r_base == 0) return abi.EINVAL;
    if (g_initialized) return abi.EBUSY;

    g_gicd = d_base;
    g_gicr = r_base;

    Gicd.Ctlr.modify(g_gicd, .{ .ns_are_ns = true, .ns_ena_grp1ns = true });
    asm volatile ("dsb sy");

    g_initialized = true;
    return 0;
}

// --- 2. Per-core initialization ------------------------------------------

pub fn initCore() callconv(.c) c_int {
    if (!g_initialized) return abi.EIO;

    const mpidr = readMpidr();
    const core_id = mpidrToCoreId(mpidr);
    if (core_id >= MAX_CORES_SUPPORTED) return abi.EINVAL;

    s_core_topology[core_id].mpidr = mpidr;
    s_core_topology[core_id].allocated = true;

    var ret = gicv3InitRedistributor();
    if (ret != 0) return ret;
    ret = gicv3InitCpuInterfaceNs();
    if (ret != 0) return ret;

    Vbar.write(@intFromPtr(&exception_vector_table));
    asm volatile ("isb");

    return 0;
}

// --- 3. Priority mask (per-core) -----------------------------------------

pub fn setCorePriorityMask(mask: u32) callconv(.c) c_int {
    iccWritePmrEl1(mask);
    return 0;
}

// --- 4. Interrupt enable / disable ---------------------------------------

pub fn enable(vector: u32) callconv(.c) c_int {
    if (vector >= MAX_INTERRUPT_VECTORS) return abi.EINVAL;

    if (vector < 32) {
        Gicr.Isenabler.writeOneHot(getCurrentGicrBase(), vector);
    } else {
        Gicd.Isenabler.writeOneHot(g_gicd, vector);
    }
    asm volatile ("dsb sy");
    return 0;
}

pub fn disable(vector: u32) callconv(.c) c_int {
    if (vector >= MAX_INTERRUPT_VECTORS) return abi.EINVAL;

    if (vector < 32) {
        Gicr.Icenabler.writeOneHot(getCurrentGicrBase(), vector);
    } else {
        Gicd.Icenabler.writeOneHot(g_gicd, vector);
    }
    asm volatile ("dsb sy");
    return 0;
}

// --- 5. Interrupt configuration (trigger, priority) ----------------------

pub fn configure(vector: u32, trigger: abi.IrqTrigger, priority: u32) callconv(.c) c_int {
    if (vector >= MAX_INTERRUPT_VECTORS) return abi.EINVAL;

    const trigger_val: u2 = if (trigger == .edge) 0b10 else 0b00;
    const prio: u8 = @truncate(priority);

    if (vector < 32) {
        const base = getCurrentGicrBase();
        Gicr.Ipriorityr.write(base, vector, prio);
        Gicr.Icfgr.write(base, vector, trigger_val);
        Gicr.Igroupr.write(base, vector, true);
        Gicr.Igrpmodr.write(base, vector, false); // Non-Secure.
    } else {
        Gicd.Ipriorityr.write(g_gicd, vector, prio);
        Gicd.Icfgr.write(g_gicd, vector, trigger_val);
        Gicd.Igroupr.write(g_gicd, vector, true);
        Gicd.Igrpmodr.write(g_gicd, vector, false); // Non-Secure.
    }

    asm volatile ("dsb sy" ::: .{ .memory = true });
    return 0;
}

// --- 6. Group assignment --------------------------------------------------

pub fn setGroup(vector: u32, group: abi.IrqGroup) callconv(.c) c_int {
    if (vector >= MAX_INTERRUPT_VECTORS) return abi.EINVAL;

    const non_secure = group == .non_secure;
    if (vector < 32) {
        Gicr.Igroupr.write(getCurrentGicrBase(), vector, non_secure);
    } else {
        Gicd.Igroupr.write(g_gicd, vector, non_secure);
    }
    asm volatile ("dsb sy" ::: .{ .memory = true });
    return 0;
}

// --- 7. SPI routing to a core ---------------------------------------------

pub fn routeToCore(vector: u32, mpidr: u64) callconv(.c) c_int {
    if (vector < 32 or vector >= MAX_INTERRUPT_VECTORS) return abi.EINVAL;

    var found = false;
    for (s_core_topology) |core| {
        if (core.allocated and core.mpidr == mpidr) {
            found = true;
            break;
        }
    }
    if (!found) return abi.EINVAL;

    // Clear IRM bit (bit 31) to force unicast delivery.
    const routing_val = mpidr & ~(@as(u64, 1) << 31);
    Gicd.Irouter.write(g_gicd, vector, routing_val);
    asm volatile ("dsb sy" ::: .{ .memory = true });
    return 0;
}

// --- 8. Acknowledge & End Of Interrupt ------------------------------------

pub fn acknowledge() callconv(.c) u32 {
    return @truncate(iccReadIar1El1() & 0xFFFFFFFF);
}

pub fn eoi(vector: u32) callconv(.c) c_int {
    iccWriteEoir1El1(vector);
    return 0;
}

// --- 9. Handler registration -----------------------------------------------

pub fn registerHandler(vector: u32, handler: ?abi.IsrHandler, arg: ?*anyopaque) callconv(.c) c_int {
    if (vector >= MAX_INTERRUPT_VECTORS or handler == null) return abi.EINVAL;

    s_isr_lock.lock();
    if (s_isr_table[vector].handler != null) {
        s_isr_lock.unlock();
        return abi.EBUSY;
    }
    s_isr_table[vector].handler = handler;
    s_isr_table[vector].arg = arg;
    s_isr_lock.unlock();

    const ret = enable(vector);
    if (ret != 0) {
        s_isr_lock.lock();
        s_isr_table[vector].handler = null;
        s_isr_table[vector].arg = null;
        s_isr_lock.unlock();
    }
    return ret;
}

pub fn unregisterHandler(vector: u32) callconv(.c) c_int {
    if (vector >= MAX_INTERRUPT_VECTORS) return abi.EINVAL;

    const ret = disable(vector);
    if (ret != 0) return ret;

    s_isr_lock.lock();
    s_isr_table[vector].handler = null;
    s_isr_table[vector].arg = null;
    s_isr_lock.unlock();
    return 0;
}

// --- 10. C-level interrupt dispatcher ---------------------------------------

export fn c_interrupt_handler() callconv(.c) void {
    const iar = iccReadIar1El1();
    const id: u32 = @truncate(iar & 0xFFFFFFFF);

    if (id == 1023) return; // spurious

    if (id < MAX_INTERRUPT_VECTORS) {
        s_isr_lock.lock();
        const handler = s_isr_table[id].handler;
        const arg = s_isr_table[id].arg;
        s_isr_lock.unlock();

        if (handler) |h| {
            h(arg);
        } else {
            kernel_fmt.print(serial_if, "Unhandled Interrupt ID: {d}\n", .{id});
        }
    } else {
        kernel_fmt.print(serial_if, "Interrupt ID out of bounds: {d}\n", .{id});
    }

    iccWriteEoir1El1(iar);
}

// --- 11. Helper to read current MPIDR (useful for the OS) ------------------

pub fn getCurrentMpidr() u64 {
    return readMpidr();
}

comptime {
    abi.exportInterface("gic_v3", abi.InterruptManager, .{
        .init_global = initGlobal,
        .init_core = initCore,
        .set_core_priority_mask = setCorePriorityMask,
        .enable = enable,
        .disable = disable,
        .configure = configure,
        .set_group = setGroup,
        .route_to_core = routeToCore,
        .acknowledge = acknowledge,
        .end_of_interrupt = eoi,
        .register_handler = registerHandler,
        .unregister_handler = unregisterHandler,
    });
}

comptime {
    _ = @import("test.zig");
}
