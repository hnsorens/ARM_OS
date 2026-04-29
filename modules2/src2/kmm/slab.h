#ifndef SLAB_H
#define SLAB_H

#include "heap.h"

typedef struct slab_allocator_t
{
    unsigned long block_size;
    void * freelist_top;
    unsigned long block_count;
} slab_allocator_t;

slab_allocator_t *create_slab_allocator(heap_t *heap, unsigned long block_size,
                                        int order);
void *slab_alloc(slab_allocator_t *slab);
void slab_free(slab_allocator_t *slab, void *addr);

#endif
