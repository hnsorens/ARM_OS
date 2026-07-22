#ifndef GIC_V3_H
#define GIC_V3_H

#include "api/gic_v3.h"
#include <type.h>

#define MAX_INTERRUPT_VECTORS 1024

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

#define MAX_CORES_SUPPORTED 64

typedef struct {
    bool     allocated;
    uint64_t mpidr;
} gic_core_map_t;


typedef struct {
    isr_handler_t handler;
    void* arg;
} registered_isr_t;


int init_global(uintptr_t d_base, uintptr_t r_base);
int init_core(void);
int set_core_priority_mask(irq_prio_t mask);
int enable(irq_vector_t vector);
int disable(irq_vector_t vector);
int configure(irq_vector_t, irq_trigger_t trigger, irq_prio_t priority);
int set_group(irq_vector_t vector, irq_group_t group);
int route_to_core(irq_vector_t vector, uint64_t mpidr);
irq_vector_t acknowledge(void);
int eoi(irq_vector_t vector);

int register_handler(irq_vector_t vector, isr_handler_t handler, void* arg);
int unregister_handler(irq_vector_t vector);

#endif
