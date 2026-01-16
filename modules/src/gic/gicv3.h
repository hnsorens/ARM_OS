#ifndef GICV3_H
#define GICV3_H

#include <stdint.h>
#include <stddef.h>    // For NULL

// IRQ types
typedef enum {
    IRQ_TYPE_LEVEL = 0,
    IRQ_TYPE_EDGE,
} irq_type_t;

// Handler callback
typedef void (*irq_handler_t)(int irq, void *data);

// Simple spinlock (implement in C)
typedef struct {
    volatile uint32_t lock;
} spinlock_t;

void spinlock_init(spinlock_t *lock);
void spinlock_acquire(spinlock_t *lock);
void spinlock_release(spinlock_t *lock);

// API
int gicv3_init(uintptr_t dist_base, uintptr_t redist_base);
int gicv3_request_irq(int irq, irq_type_t type, irq_handler_t handler, void *data);
void gicv3_free_irq(int irq);
void gicv3_enable_irq(int irq);
void gicv3_disable_irq(int irq);
int gicv3_set_affinity(int irq, uint64_t mpidr);
void gicv3_global_enable(void);
void gicv3_global_disable(void);
void gicv3_handle_irq(void);   // Call from vector table

#endif // GICV3_H