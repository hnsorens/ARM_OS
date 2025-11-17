#ifndef BUDDY_ALLOCATOR_H
#define BUDDY_ALLOCATOR_H

#include <Uefi.h>
#include "data_structures/bitmap.h"
#include <stdint.h>

#define MAX_ORDER 256

typedef struct buddy_section_t
{
  bitmap_t* bitmap;
  uintptr_t top;
} buddy_section_t;

typedef struct buddy_allocator_t
{
  buddy_section_t sections[MAX_ORDER];
} buddy_allocator_t;

void buddy_init(EFI_MEMORY_DESCRIPTOR* memory_map, size_t region_count, buddy_allocator_t *allocator, uintptr_t buddy_memory,size_t total_memory);

size_t buddy_get_memory_size(size_t total_memory);

#endif
