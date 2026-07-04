#ifndef GIC_V3_H
#define GIC_V3_H

#include <type.h>

int enable(u64 irq_vector);
int disable(u64 irq_vector);
int ack(u64 irq_vector);
int eoi(u64 irq_vector);

// Configuration Base Addresses
typedef struct {
    uintptr_t gicd_base;
    uintptr_t gicr_base;
} gicv3_mmo_t;

// Public Interface
int gicv3_init_base(const gicv3_mmo_t *mmo);

int gicv3_init_global(void);
int gicv3_init_redistributor(void);
int gicv3_init_cpu_interface_secure(void);
void driver_timer_start_sequence(void);

#endif
