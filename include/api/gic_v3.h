#ifndef IRQ_CHIP_API_H
#define IRQ_CHIP_API_H

#include <type.h>

typedef struct irq_chip_interface {
    int (*enable)(u64 irq_vector);
    int (*disable)(u64 irq_vector);
    int (*ack)(u64 irq_vector);
    int (*eoi)(u64 irq_vector); // End of Interrupt
} irq_chip_interface_t;

#endif
