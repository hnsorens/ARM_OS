#ifndef HEAP_API_H
#define HEAP_API_H

#include "../type.h"

struct heap_context;

typedef struct heap_interface {
    k_status_t (*create)(paddr_t root, size_t sz, struct heap_context **out_heap);
    k_status_t (*destroy)(struct heap_context *heap);
    k_status_t (*malloc)(struct heap_context *heap, size_t size, void **out_ptr);
    k_status_t (*free)(struct heap_context *heap, void *ptr);
    k_status_t (*realloc)(struct heap_context *heap, void *ptr, size_t new_size, void **out_ptr);
    k_status_t (*memalign)(struct heap_context *heap, size_t alignment, size_t size, void **out_ptr);
    k_status_t (*get_stats)(struct heap_context *heap, size_t *used, size_t *total);
} heap_interface_t;

#endif
