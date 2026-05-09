#ifndef SLAB_ALLOCATOR_API_H
#define SLAB_ALLOCATOR_API_H

#include "../type.h"

typedef struct slab_interface {
    // Creates a new 'pool' for a specific object size
    // Example: slab_create("task_cache", sizeof(task_t), 16);
    k_status_t (*create_cache)(const char *name, size_t obj_size, size_t alignment);

    // Destroys a cache and returns all pages to the VMM/PMM
    k_status_t (*destroy_cache)(const char *name);

    // Grabs one object-sized slot from the cache
    void* (*alloc)(const char *name);

    // Returns the slot to the cache for reuse
    void (*free)(const char *name, void *obj);

    // Optional: Reclaims empty slabs if the system is low on memory
    k_status_t (*shrink)(const char *name);
} slab_module_interface_t;

#endif
