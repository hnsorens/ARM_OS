.section .vectors, "ax"
.align 11
.global vectors
.extern gic_handle_irq

vectors:
    b   sync_handler_sp0
    .align 7
    b   irq_handler_sp0
    .align 7      
    b   irq_handler_sp0
    .align 7
    b   irq_handler_sp0
    
    .align 7
    b   sync_handler_sp0
    .align 7
    b   irq_handler_sp0
    .align 7      
    b   irq_handler_sp0
    .align 7
    b   irq_handler_sp0

    .skip 0x80

sync_handler_sp0:
    // Put debug pattern in registers to see in debugger
    mov x0, #0xBAD         // Mark: sync handler entered
    mrs x1, esr_el1        // Exception syndrome
    mrs x2, elr_el1        // PC where exception happened
    mrs x3, far_el1        // Fault address (if any)
    mrs x4, spsr_el1       // Saved processor state
    mrs x5, sp_el0         // Current stack
    
    // Hang - registers x0-x5 now contain debug info
    b   .

irq_handler_sp0:
    /* Save exception state to temporary registers */
    mrs x2, spsr_el1      // Use x2 as temp for spsr
    mrs x3, elr_el1       // Use x3 as temp for elr
    
    /* Save ALL registers in correct order */
    stp x0, x1, [sp, #-16]!   // Save ORIGINAL x0,x1
    stp x2, x3, [sp, #-16]!   // Save spsr/elr from x2,x3
    stp x4, x5, [sp, #-16]!
    stp x6, x7, [sp, #-16]!
    stp x8, x9, [sp, #-16]!
    stp x10, x11, [sp, #-16]!
    stp x12, x13, [sp, #-16]!
    stp x14, x15, [sp, #-16]!
    stp x16, x17, [sp, #-16]!
    stp x18, x19, [sp, #-16]!
    stp x20, x21, [sp, #-16]!
    stp x22, x23, [sp, #-16]!
    stp x24, x25, [sp, #-16]!
    stp x26, x27, [sp, #-16]!
    stp x28, x29, [sp, #-16]!
    stp x30, xzr, [sp, #-16]!
    
    bl gic_handle_irq
    
    /* Restore ALL registers in REVERSE order */
    ldp x30, xzr, [sp], #16
    ldp x28, x29, [sp], #16
    ldp x26, x27, [sp], #16
    ldp x24, x25, [sp], #16
    ldp x22, x23, [sp], #16
    ldp x20, x21, [sp], #16
    ldp x18, x19, [sp], #16
    ldp x16, x17, [sp], #16
    ldp x14, x15, [sp], #16
    ldp x12, x13, [sp], #16
    ldp x10, x11, [sp], #16
    ldp x8, x9, [sp], #16
    ldp x6, x7, [sp], #16
    ldp x4, x5, [sp], #16
    ldp x2, x3, [sp], #16    // x2=spsr, x3=elr
    ldp x0, x1, [sp], #16    // ORIGINAL x0,x1
    
    msr elr_el1, x3
    msr spsr_el1, x2
    
    eret