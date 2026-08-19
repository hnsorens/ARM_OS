//! GICv3 interrupt controller driver, exporting `InterruptManager`
//! (category "interrupt_manager"). Supports multi-core (GICv3
//! redistributors), SPI routing, and SGI/PPI configuration.
const std = @import("std");
const abi = @import("abi");
const kernel_fmt = @import("kernel_fmt");
const kernel_test = @import("kernel_test");
const mmio = @import("mmio");

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
var s_isr_lock: u64 = 0;

// The exclusive-store status result of `stlxr` must be a 32-bit (W)
// register no matter the data size, but Zig's inline asm has no template
// modifier to request a sub-register view of a named operand (`%w[name]`
// doesn't parse, and `"r"` constraints always allocate a full X register
// here) -- so the status is bound to a hardcoded physical register (w9)
// instead, with x9 declared clobbered.
fn spinLock(s: *volatile u64) void {
    _ = asm volatile (
        \\1: ldaxr %[ret], [%[addr]]
        \\   cbnz %[ret], 1b
        \\   stlxr w9, %[one], [%[addr]]
        \\   cbnz w9, 1b
        : [ret] "=&r" (-> u64),
        : [addr] "r" (s),
          [one] "r" (@as(u64, 1)),
        : .{ .memory = true, .x9 = true });
}

fn spinUnlock(s: *volatile u64) void {
    asm volatile ("stlr %[zero], [%[addr]]"
        :
        : [zero] "r" (@as(u64, 0)),
          [addr] "r" (s),
        : .{ .memory = true });
}

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

const ICC_SRE_SRE: u64 = 1 << 0;
const ICC_SRE_DFB: u64 = 1 << 1;
const ICC_SRE_DIB: u64 = 1 << 2;
const ICC_IGRPEN1_ENABLE: u64 = 1 << 0;

fn mpidrToCoreId(mpidr: u64) u32 {
    const aff0: u32 = @truncate(mpidr & 0xFF);
    const aff1: u32 = @truncate((mpidr >> 8) & 0xFF);
    return aff0 + aff1; // single cluster => linear index
}

pub fn readMpidr() u64 {
    return asm volatile ("mrs %[out], mpidr_el1"
        : [out] "=r" (-> u64),
    );
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
    var sre = asm volatile ("mrs %[out], ICC_SRE_EL1"
        : [out] "=r" (-> u64),
    );
    sre |= ICC_SRE_SRE | ICC_SRE_DFB | ICC_SRE_DIB;
    asm volatile ("msr ICC_SRE_EL1, %[v]"
        :
        : [v] "r" (sre),
    );
    asm volatile ("isb");

    asm volatile ("msr ICC_PMR_EL1, %[v]"
        :
        : [v] "r" (@as(u64, 0xFF)),
    );
    asm volatile ("msr ICC_BPR1_EL1, %[v]"
        :
        : [v] "r" (@as(u64, 0)),
    );
    asm volatile ("msr ICC_CTLR_EL1, %[v]"
        :
        : [v] "r" (@as(u64, 0)),
    );
    asm volatile ("msr ICC_IGRPEN1_EL1, %[v]"
        :
        : [v] "r" (ICC_IGRPEN1_ENABLE),
    );
    asm volatile ("dsb sy");
    asm volatile ("isb");
    return 0;
}

fn iccWritePmrEl1(val: u64) void {
    asm volatile ("msr ICC_PMR_EL1, %[v]"
        :
        : [v] "r" (val),
    );
    asm volatile ("isb");
}

fn iccReadIar1El1() u64 {
    return asm volatile ("mrs %[out], ICC_IAR1_EL1"
        : [out] "=r" (-> u64),
    );
}

fn iccWriteEoir1El1(val: u64) void {
    asm volatile ("msr ICC_EOIR1_EL1, %[v]"
        :
        : [v] "r" (val),
    );
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

    asm volatile ("msr vbar_el1, %[v]"
        :
        : [v] "r" (@intFromPtr(&exception_vector_table)),
    );
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

    spinLock(&s_isr_lock);
    if (s_isr_table[vector].handler != null) {
        spinUnlock(&s_isr_lock);
        return abi.EBUSY;
    }
    s_isr_table[vector].handler = handler;
    s_isr_table[vector].arg = arg;
    spinUnlock(&s_isr_lock);

    const ret = enable(vector);
    if (ret != 0) {
        spinLock(&s_isr_lock);
        s_isr_table[vector].handler = null;
        s_isr_table[vector].arg = null;
        spinUnlock(&s_isr_lock);
    }
    return ret;
}

pub fn unregisterHandler(vector: u32) callconv(.c) c_int {
    if (vector >= MAX_INTERRUPT_VECTORS) return abi.EINVAL;

    const ret = disable(vector);
    if (ret != 0) return ret;

    spinLock(&s_isr_lock);
    s_isr_table[vector].handler = null;
    s_isr_table[vector].arg = null;
    spinUnlock(&s_isr_lock);
    return 0;
}

// --- 10. C-level interrupt dispatcher ---------------------------------------

export fn c_interrupt_handler() callconv(.c) void {
    const iar = iccReadIar1El1();
    const id: u32 = @truncate(iar & 0xFFFFFFFF);

    if (id == 1023) return; // spurious

    if (id < MAX_INTERRUPT_VECTORS) {
        spinLock(&s_isr_lock);
        const handler = s_isr_table[id].handler;
        const arg = s_isr_table[id].arg;
        spinUnlock(&s_isr_lock);

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
