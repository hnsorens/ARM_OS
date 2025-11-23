
#ifndef BUDDY_ALLOCATOR_H
#define BUDDY_ALLOCATOR_H

#include "bitmap.h"
#include "../module_vtables.h"

#define MAX_ORDER 256

typedef struct buddy_section_t
{
  bitmap_t* bitmap;
  unsigned long top;
  unsigned long block_count;
} buddy_section_t;

typedef struct buddy_allocator_t
{
  buddy_section_t sections[MAX_ORDER];
  unsigned long section_count;
} buddy_allocator_t;

void buddy_init(memory_region_t* memory_map, size_t region_count, buddy_allocator_t *allocator, uintptr_t buddy_memory,size_t total_memory);
size_t buddy_get_memory_size(size_t total_memory);

uintptr_t buddy_alloc_phys(buddy_allocator_t *allocator, size_t order);
void buddy_free_phys(buddy_allocator_t *allocator, uintptr_t addr, size_t order);

void* buddy_alloc_kernel(buddy_allocator_t* allocator, unsigned long virt_addr, unsigned long size, vmm_vtable_t* vmm);
void buddy_free_kernel(buddy_allocator_t* allocator, unsigned long virt_addr, unsigned long size, vmm_vtable_t* vmm);
unsigned long buddy_memory_available(buddy_allocator_t* allocator);

#endif
