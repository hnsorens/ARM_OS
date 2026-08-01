#ifndef TIMER_API_H
#define TIMER_API_H

#include <stdint.h>
#include <type.h>

typedef uint32_t timer_id_t;

typedef void (*timer_callback_t)(timer_id_t id, void* context);

// The Universal Timer VTable
typedef struct timer_interface {
    int (*register_callback)(uint32_t ticks_period, uint8_t periodic, timer_callback_t callback, timer_id_t* id, void *context);
    int (*unregister_callback)(timer_id_t id);
    int (*pause)(timer_id_t id);
    int (*resume)(timer_id_t id);
    int (*modify)(timer_id_t id, uint32_t new_period);
    int (*get_system_ticks)(uint64_t *ticks);
    int (*delay_ticks)(uint32_t ticks);
} timer_interface_t;

#endif

