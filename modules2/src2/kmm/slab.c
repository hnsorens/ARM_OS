#include "slab.h"

#include "pmm/pmm_inc.h"

slab_allocator_t *create_slab_allocator(heap_t* heap, unsigned long block_size, int allocation_order)
{
    // Allocate initial page for allocator
    void *page = pmm_alloc_phys(allocation_order);
    slab_allocator_t * allocator = (slab_allocator_t*)heap_malloc(heap, sizeof(slab_allocator_t));
    
    allocator->block_count = (4096 << allocation_order) / block_size;
    allocator->freelist_top = page;

    unsigned long *current = page;
    for (int i = 0; i < allocator->block_count; i++)
    {
	if (i == allocator->block_count - 1)
	{
	    *current = 0;
	} else
	{
	    *current = (unsigned long)current + block_size;
	}
    }

    return allocator;
}

void *slab_alloc(slab_allocator_t *slab)
{
    if (slab->freelist_top == 0) return 0;

    void* return_address = slab->freelist_top;
    slab->freelist_top = (void*)*(unsigned long*)(slab->freelist_top);
    return return_address;
}

void slab_free(slab_allocator_t *slab, void *addr) {
  if (!addr)
      return;

  *(unsigned long *)addr = (unsigned long)slab->freelist_top;
  slab->freelist_top = addr;
  
}    