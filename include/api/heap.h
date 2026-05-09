#ifndef HEAP_H
#define HEAP_H

#include "../type.h"

typedef struct heap_interface {
    // Standard byte-level allocation
    void* (*malloc)(size_t size);

    // Standard free
    void (*free)(void *ptr);

    // Reallocates to a new size, copying data if necessary
    void* (*realloc)(void *ptr, size_t new_size);

    // Allocation with specific alignment (often used for buffers)
    void* (*memalign)(size_t alignment, size_t size);

    // Stats for debugging memory leaks
    void (*get_stats)(size_t *used, size_t *total);
} heap_interface_t;

#endif
