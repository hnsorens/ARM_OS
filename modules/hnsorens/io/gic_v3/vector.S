.balign 2048
.global exception_vector_table
exception_vector_table:
    /* --- Current EL with SP0 (Offsets 0x000 - 0x180) --- */
    .balign 128
    b irq_vector_entry // Synchronous
    .balign 128
    b irq_vector_entry // IRQ
    .balign 128
    b irq_vector_entry // FIQ
    .balign 128
    b irq_vector_entry // SError

    /* --- Current EL with SPx (Offsets 0x200 - 0x380) --- */
    .balign 128
    b irq_vector_entry // Synchronous
    .balign 128
    b irq_vector_entry  // <--- THIS IS WHERE THE TIMER INTERRUPT LANDS!
    .balign 128
    b irq_vector_entry // FIQ
    .balign 128
    b irq_vector_entry // SError

// This macro pushes all general-purpose registers onto the stack
.macro save_context
    sub sp, sp, #256
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]      // Save Link Register (Return Address)
.endm

// This macro restores them before returning
.macro restore_context
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp x8, x9, [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    ldr x30, [sp, #240]
    add sp, sp, #256
.endm

.global irq_vector_entry
irq_vector_entry:
    save_context
    
    bl c_interrupt_handler  // Jump to your C code!
    
    restore_context
    eret                    // Exception Return: Safely jumps back to regular execution
