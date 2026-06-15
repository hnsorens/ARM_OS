#ifndef HEAP_API_H
#define HEAP_API_H

#include <type.h>

struct heap_context;

typedef struct heap_interface {
    int (*create)(u64 root, u64 sz, struct heap_context **out_heap);
    int (*destroy)(struct heap_context *heap);
    int (*malloc)(struct heap_context *heap, u64 size, void **out_ptr);
    int (*free)(struct heap_context *heap, void *ptr);
    int (*realloc)(struct heap_context *heap, void *ptr, u64 new_size, void **out_ptr);
    int (*memalign)(struct heap_context *heap, u64 alignment, u64 size, void **out_ptr);
    int (*get_stats)(struct heap_context *heap, u64 *used, u64 *total);
} heap_interface_t;

#endif
