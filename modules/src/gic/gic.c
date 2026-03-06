#include "gic/gic_impl.h"

#include <stdint.h>
#include "gicv3.h"

#include "pmm/pmm_inc.h"
#include "serial_debug/serial_debug_inc.h"

#define GICD_BASE 0x08000000ULL
#define GICR_BASE 0x080A0000ULL

// Define system register access macros (needed here because timer code is here)
#define SYS_READ(reg, val)  __asm__ volatile("mrs %0, " #reg : "=r"(val))
#define SYS_WRITE(reg, val) __asm__ volatile("msr " #reg ", %0" : : "r"(val) : "memory")

// ARM Generic Timer system register names
// Note: Some compilers/toolchains use different names; these are standard
#define CNTPCT_FRQ_EL0   CNTPCT_FRQ_EL0   // Timer frequency
#define CNTV_TVAL_EL0    CNTV_TVAL_EL0
#define CNTV_CTL_EL0     CNTV_CTL_EL0

void timer_handler(int irq, void *data) {
    // Acknowledge timer (system-specific, e.g., CNTV_CTL_EL0)
    uint64_t ctl;
    SYS_READ(CNTV_CTL_EL0, ctl);
    SYS_WRITE(CNTV_CTL_EL0, ctl & ~1);  // Disable temporarily


    // Do something, e.g., print "Timer fired!"
    SYS_WRITE(CNTV_CTL_EL0, ctl | 1);  // Re-enable
}

override void gic_fetch(core_ops* ops)
{
    serial_debug_fetch(ops);
}

override void gic_start(core_ops* ops)
{
    DEBUG("Init");

	extern void _vectors(void);
    uintptr_t vaddr = (uintptr_t)_vectors;
    __asm__ volatile("msr vbar_el1, %0" : : "r"(vaddr + __load_offset__));
    __asm__ volatile("isb");
    
    gicv3_init(GICD_BASE, GICR_BASE);

	// // Init GICv3 (call per-core in SMP boot)
    // gicv3_init(GICD_BASE, GICR_BASE);

    // // Request basic interrupt: Non-secure physical timer (PPI 30, level-triggered)
    // // gicv3_request_irq(30, IRQ_TYPE_LEVEL, timer_handler, NULL);
    // gicv3_request_irq(30, IRQ_TYPE_LEVEL, timer_handler, NULL);

    // // Enable interrupts globally
    // // Setup timer as test (e.g., 1 second interval)
    // SYS_WRITE(CNTFRQ_EL0, 100000000);  // Assume 100MHz freq
    // SYS_WRITE(CNTV_TVAL_EL0, 1000);  // Load value
    // SYS_WRITE(CNTV_CTL_EL0, 0x1);

    // gicv3_global_enable();
    
    // DEBUG("GIC initialized\n");

}

override void gic_init(gic_ops* ops)
{

}
