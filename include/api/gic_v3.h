#ifndef INTERRUPT_MANAGER_API_H
#define INTERRUPT_MANAGER_API_H

#include <type.h>

typedef uint32_t irq_vector_t;
typedef uint32_t irq_prio_t;

typedef enum {
    IRQ_TRIGGER_LEVEL,
    IRQ_TRIGGER_EDGE
} irq_trigger_t;

typedef enum {
    IRQ_GROUP_SECURE,
    IRQ_GROUP_NON_SECURE
} irq_group_t;

typedef void (*isr_handler_t)(void* context);

// The Universal Interrupt Manager VTable
typedef struct interrupt_manager_interface {
    /* --- System-Wide / Global Init --- */
    int (*init_global)(uintptr_t d_base, uintptr_t r_base);

    /* --- Per-Core Interface Management --- */
    int (*init_core)(void);
    int (*set_core_priority_mask)(irq_prio_t mask);

    /* --- Interrupt Routing & Configuration --- */
    int (*enable)(irq_vector_t vector);
    int (*disable)(irq_vector_t vector);
    int (*configure)(irq_vector_t vector, irq_trigger_t trigger, irq_prio_t priority);
    int (*set_group)(irq_vector_t vector, irq_group_t group);
    int (*route_to_core)(irq_vector_t vector, uint64_t mpidr_or_apicid);

    /* --- Interrupt Lifecycle (Called inside Assembly Exception Entry) --- */
    irq_vector_t (*acknowledge)(void);
    int (*end_of_interrupt)(irq_vector_t vector);

    // Registers a software function pointer to a specific hardware vector
    int (*register_handler)(irq_vector_t vector, isr_handler_t handler, void* arg);
    int (*unregister_handler)(irq_vector_t vector);
} interrupt_manager_interface_t;

#endif
