#include "buddy.h"
#include "bitmap.h"
#include <stdint.h>

buddy_allocator_t allocator;

size_t total_memory = 0;

static size_t find_max_block_size(void *addr, void *end) {
    size_t max_size = end - addr;
    size_t block_size = 4096;
    size_t best_size = 4096;
    
    while (block_size <= max_size) {
        if (((uint64_t)addr & (block_size - 1)) == 0) {
            best_size = block_size;
        }
        if (block_size > max_size / 2) break;
        block_size *= 2;
    }
    return best_size;
}

static void add_block_to_freelist(buddy_section_t *section, void *addr, size_t order)
{
  if (section->top) *((void **)section->top + 8) = addr;

  *((void **)addr) = section->top;
  *((void **)(addr + 8)) = 0;
  section->top = addr;
  section->block_count++;

  size_t block_size = (1UL << order) * 4096;
  size_t bitmap_index = (uint64_t)addr / block_size;
  bitmap_set(section->bitmap, bitmap_index);
}

size_t buddy_get_memory_size(size_t total_memory)
{
    size_t buddy_size = 0;
    size_t block_size = 4096;
    {
	
    }

  while (block_size <= total_memory)
  {
    size_t block_count = (total_memory / block_size) + 1;
    buddy_size += bitmap_memory_size(block_count);

    if (block_size > total_memory / 2) break;
    block_size *= 2;
  }

  return buddy_size;
}

static size_t get_total_memory(memory_region_t *memory_map, size_t region_count) {

    size_t current_size = 0;
    for (int i = 0; i < region_count; i++)
    {
        current_size += memory_map[i].size;
    }
    return current_size;
}

void buddy_init(memory_region_t* memory_map, size_t region_count)
{
    total_memory = get_total_memory(memory_map, region_count);


  for (int i = 0; i < region_count; i++)
  {
    if (memory_map->memory_type == MEMORY_FREE)
    {

      for (int i2 = 0; i2 < memory_map[i].size; i2++)
      {
        ((char*)memory_map[i].start)[i2] = 0;
      }
    }
  }
  size_t block_size = 4096;
  void *buddy_memory_pos = 0;
  uint32_t order = 0;

  allocator.section_count = 0;

  while (block_size <= total_memory)
  {
    allocator.section_count++;
    size_t block_count = (total_memory / block_size) + 1;
    allocator.sections[order].bitmap = bitmap_create(buddy_memory_pos, block_count);
    order++;
    if (block_size > total_memory / 2) break;
    block_size *= 2;
    buddy_memory_pos += bitmap_memory_size(block_count);
  }

  for (int i = 0; i < MAX_ORDER; i++)
  {
    allocator.sections[i].top = 0;
    allocator.sections[i].block_count = 0;
  }

  for (int i = 0; i < region_count; i++) {
    if (memory_map[i].memory_type == MEMORY_FREE && memory_map[i].start > 0x40000000 && memory_map[i].start < 0x400000000)
    {
      void *block_end = memory_map[i].start + memory_map[i].size * 4096;
      void *block_position = memory_map[i].start;

      while (block_position < block_end)
      {
        block_size = find_max_block_size(block_position, block_end);
        if (block_size < 4096) break;
        // handle free
        uint64_t section_index = (63 - __builtin_clzll(block_size / 4096));
        uint64_t page_index = (uint64_t)block_position / block_size;
        bitmap_set(allocator.sections[section_index].bitmap, page_index);
        if (allocator.sections[section_index].top) {
          *((void**)allocator.sections[section_index].top + 8) = block_position;
        }

        *((void**)block_position) = allocator.sections[section_index].top;
        allocator.sections[section_index].top = block_position;
        allocator.sections[section_index].block_count++;

        
        block_position += block_size;
      }
    }
  }
}

static void remove_block_from_freelist(buddy_section_t* section, void *addr, size_t order)
{
  if (order >= MAX_ORDER || addr == 0)
  {
    return;
  }

  void* next = *((void **)addr);
  void* prev = *((void **)addr + 8);

   if (prev != 0) {
      *((void**)prev) = next;  // prev->next = next
  } else {
      section->top = next;         // This was the head
  }

  // Update the next block's prev pointer
  if (next != 0) {
      *((void **)next + 8) = prev;  // next->prev = prev
  }

  section->block_count--;

  if (section->block_count == 0)
  {
    section->top = 0;
  }

  // Clear the bitmap
  size_t block_size = (1UL << order) * 4096;
  size_t bitmap_index = (uint64_t)addr / block_size;
  bitmap_clear(section->bitmap, bitmap_index);
}

void *buddy_alloc_phys_exact(unsigned long actual_size)
{
  
  unsigned long memory1 = buddy_memory_available();
    // Find the minimum order that can fit the size
    size_t order = 0;
    size_t block_size = 4096;
    
    while (block_size < actual_size) {
        if (order >= MAX_ORDER - 1) break;
        order++;
        block_size *= 2;
    }


    // Allocate from that order (ONE allocation)
    phys_addr_t block_addr = 0;
    buddy_alloc_page(order, &block_addr);

    if (!block_addr)
    {
      // TODO implement fragmentation allocation (this will be needed)
      return 0;
    }
    
    // If we got exactly what we need, return it
    if (block_size == actual_size) {
        return (void*)block_addr;
    }
    
    void *free_position = (void *)(block_addr + actual_size);
    void *block_end = (void *)(block_addr + block_size);
    
    unsigned long space_allocated = 0;
    while (free_position < block_end) {
        size_t free_size = find_max_block_size(free_position, block_end);
        if (free_size < 4096) break;
        
        size_t free_order = (63 - __builtin_clzll(free_size / 4096));
        buddy_free_page((phys_addr_t)free_position, free_order);

        space_allocated += free_size;
        
        free_position += free_size;
    }
 
    return (void*)block_addr;
}

k_status_t buddy_alloc_page(uint8_t order, phys_addr_t *dest)
{
  if (order >= MAX_ORDER || !dest)
  {
    return K_STATUS_INVALID_ARG;
  }
  int current_order = order;
  while (current_order < MAX_ORDER)
  {
    buddy_section_t *section = &allocator.sections[current_order];
    if (section->block_count != 0)
    {
      void *block_addr = section->top;
      section->top = *((void**)block_addr);
      if (section->top) *((unsigned long*)section->top + 8) = 0;
      section->block_count--;

      size_t block_size = (1UL << current_order) * 4096;
      size_t bitmap_index = (uint64_t)block_addr / block_size;
      
      bitmap_clear(section->bitmap, bitmap_index);
      if (current_order > order)
      {
        void *keep_addr = block_addr;
        size_t split_size = block_size;
        for (size_t split_order = current_order - 1; split_order + 1 > order; split_order--)
        {
          split_size /= 2;
          void *buddy_addr = keep_addr + split_size;
          buddy_section_t *buddy_section = &allocator.sections[split_order];
          add_block_to_freelist(buddy_section, buddy_addr, split_order);
        }
      }

      *dest = (phys_addr_t)block_addr;
      return K_STATUS_OK;
    }
    current_order++;
  }
  return K_STATUS_OUT_OF_MEMORY;
}

k_status_t buddy_free_page(uint8_t order, phys_addr_t addr)
{
    if (order >= MAX_ORDER || addr == 0) return K_STATUS_INVALID_ARG;

    size_t current_order = order;
    void *current_addr = (void*)addr;

    while (current_order < MAX_ORDER - 1) {
        size_t block_size = (1UL << current_order) * 4096;
        void *buddy_addr = (void*)((uint64_t)current_addr ^ block_size);
        
        buddy_section_t *section = &allocator.sections[current_order];
        size_t buddy_bitmap_index = (uint64_t)buddy_addr / block_size;

        if (!bitmap_test(section->bitmap, buddy_bitmap_index)) {
            break;
        }

        remove_block_from_freelist(section, buddy_addr, current_order);

        // The merged block is the LOWER address of current and buddy
        current_addr = current_addr < buddy_addr ? current_addr : buddy_addr;
        current_order++;
    }

    // Add the final coalesced block (which might be larger than original)
    buddy_section_t *final_section = &allocator.sections[current_order];
    add_block_to_freelist(final_section, current_addr, current_order);

    return K_STATUS_OK;
}

unsigned long buddy_memory_available()
{
  unsigned long section_size = 4096;
  unsigned long memory_freed = 0;
  for (int i = 0; i < allocator.section_count; i++)
  {
    memory_freed += section_size * allocator.sections[i].block_count;
    section_size *= 2;
  }
  return memory_freed;
}
