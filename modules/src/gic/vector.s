.section .vectors, "ax"
.align 11  // 2048-byte alignment for VBAR

.extern gicv3_handle_irq

.global _vectors
_vectors:

// Current EL with SP0
.org 0x000
    b synchronous_current_sp0
.org 0x080
    b irq_current_sp0
.org 0x100
    b fiq_current_sp0
.org 0x180
    b serror_current_sp0

// Current EL with SPx
.org 0x200
    b synchronous_current_spx
.org 0x280
    b irq_current_spx
.org 0x300
    b fiq_current_spx
.org 0x380
    b serror_current_spx

// Lower EL using AArch64
.org 0x400
    b synchronous_lower_aarch64
.org 0x480
    b irq_lower_aarch64
.org 0x500
    b fiq_lower_aarch64
.org 0x580
    b serror_lower_aarch64

// Lower EL using AArch32
.org 0x600
    b synchronous_lower_aarch32
.org 0x680
    b irq_lower_aarch32
.org 0x700
    b fiq_lower_aarch32
.org 0x780
    b serror_lower_aarch32

// Handlers (stubbed; expand as needed)
synchronous_current_sp0: b hang
irq_current_sp0: b hang
fiq_current_sp0: b hang
serror_current_sp0: b hang

synchronous_current_spx: b hang
irq_current_spx: b hang
fiq_current_spx: b hang
serror_current_spx: b hang

synchronous_lower_aarch64: b hang
fiq_lower_aarch64: b hang
serror_lower_aarch64: b hang

synchronous_lower_aarch32: b hang
irq_lower_aarch32: b hang
fiq_lower_aarch32: b hang
serror_lower_aarch32: b hang

// IRQ from lower EL (common for OS)
irq_lower_aarch64: b hang
irq_lower_aarch64asd:
    stp x29, x30, [sp, #-16]!
    stp x0, x1, [sp, #-16]!
    // Save more regs if needed

    bl gicv3_handle_irq  // Call C handler

    ldp x0, x1, [sp], #16
    ldp x29, x30, [sp], #16
    eret

hang:
    wfe
    b hang