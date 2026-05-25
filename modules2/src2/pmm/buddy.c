#include "buddy.h"
#include "bitmap.h"
#include <stdint.h>

#include "mmu/mmu_inc.h"
#include "serial_debug/serial_debug_inc.h"

static size_t find_max_block_size(uintptr_t addr, uintptr_t end) {
    size_t max_size = end - addr;
    size_t block_size = 4096;
    size_t best_size = 4096;
    
    while (block_size <= max_size) {
        if ((addr & (block_size - 1)) == 0) {
            best_size = block_size;
        }
        if (block_size > max_size / 2) break;
        block_size *= 2;
    }
    return best_size;
}

static void add_block_to_freelist(buddy_section_t *section, uintptr_t addr, size_t order)
{
  if (section->top) *((unsigned long*)section->top + 8) = addr;

  *((unsigned long*)addr) = section->top;
  *((unsigned long*)(addr + 8)) = 0;
  section->top = addr;
  section->block_count++;

  size_t block_size = (1UL << order) * 4096;
  size_t bitmap_index = addr / block_size;
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

void buddy_init(memory_region_t* memory_map, size_t region_count, buddy_allocator_t *allocator, uintptr_t buddy_memory, size_t total_memory)
{
  for (int i = 0; i < region_count; i++)
  {
    serial_debug_serial_printf("MEMORY MAP: START %16x, Size: %16x Free: %d\n", memory_map[i].start, memory_map[i].size, memory_map[i].memory_type == MEMORY_FREE);
    if (memory_map->memory_type == MEMORY_FREE)
    {

      for (int i2 = 0; i2 < memory_map[i].size; i2++)
      {
        ((char*)memory_map[i].start)[i2] = 0;
      }
    }
  }
  size_t block_size = 4096;
  uintptr_t buddy_memory_pos = buddy_memory;
  uint32_t order = 0;

  allocator->section_count = 0;

  while (block_size <= total_memory)
  {
    allocator->section_count++;
    size_t block_count = (total_memory / block_size) + 1;
    allocator->sections[order].bitmap = bitmap_create(buddy_memory_pos, block_count);
    order++;
    if (block_size > total_memory / 2) break;
    block_size *= 2;
    buddy_memory_pos += bitmap_memory_size(block_count);
  }

  for (int i = 0; i < MAX_ORDER; i++)
  {
    allocator->sections[i].top = 0;
    allocator->sections[i].block_count = 0;
  }

  for (int i = 0; i < region_count; i++) {
    if (memory_map[i].memory_type == MEMORY_FREE && memory_map[i].start > 0x40000000 && memory_map[i].start < 0x400000000)
    {
      uintptr_t block_end = memory_map[i].start + memory_map[i].size * 4096;
      uintptr_t block_position = memory_map[i].start;

      while (block_position < block_end)
      {
        block_size = find_max_block_size(block_position, block_end);
        if (block_size < 4096) break;
        // handle free
        uint64_t section_index = (63 - __builtin_clzll(block_size / 4096));
        uint64_t page_index = block_position / block_size;
        bitmap_set(allocator->sections[section_index].bitmap, page_index);
        if (allocator->sections[section_index].top) {
          *((unsigned long*)allocator->sections[section_index].top + 8) = block_position;
        }

        *((uintptr_t*)block_position) = allocator->sections[section_index].top;
        serial_debug_serial_printf("ADDED %lx\n", block_position);
        allocator->sections[section_index].top = block_position;
        allocator->sections[section_index].block_count++;

        
        block_position += block_size;
      }
    }
  }
}

static void remove_block_from_freelist(buddy_section_t* section, uintptr_t addr, size_t order)
{
  if (order >= MAX_ORDER || addr == 0)
  {
    return;
  }

  unsigned long next = *((unsigned long*)addr);
  unsigned long prev = *((unsigned long*)addr + 8);

   if (prev != 0) {
      *((unsigned long*)prev) = next;  // prev->next = next
  } else {
      section->top = next;         // This was the head
  }

  // Update the next block's prev pointer
  if (next != 0) {
      *((unsigned long*)next + 8) = prev;  // next->prev = prev
  }

  section->block_count--;

  if (section->block_count == 0)
  {
    section->top = 0;
  }

  // Clear the bitmap
  size_t block_size = (1UL << order) * 4096;
  size_t bitmap_index = addr / block_size;
  bitmap_clear(section->bitmap, bitmap_index);
}

unsigned long buddy_alloc_phys_exact(buddy_allocator_t* allocator, unsigned long actual_size)
{
  
  unsigned long memory1 = buddy_memory_available(allocator);
    // Find the minimum order that can fit the size
    size_t order = 0;
    size_t block_size = 4096;
    
    while (block_size < actual_size) {
        if (order >= MAX_ORDER - 1) break;
        order++;
        block_size *= 2;
    }


    // Allocate from that order (ONE allocation)
    uintptr_t block_addr = buddy_alloc_phys(allocator, order);

    if (!block_addr)
    {
      // TODO implement fragmentation allocation (this will be needed)
      return 0;
    }
    
    // If we got exactly what we need, return it
    if (block_size == actual_size) {
        return block_addr;
    }
    
    uintptr_t free_position = block_addr + actual_size;
    uintptr_t block_end = block_addr + block_size;
    
    unsigned long space_allocated = 0;
    while (free_position < block_end) {
        size_t free_size = find_max_block_size(free_position, block_end);
        if (free_size < 4096) break;
        
        size_t free_order = (63 - __builtin_clzll(free_size / 4096));
        buddy_free_phys(allocator, free_position, free_order);

        space_allocated += free_size;
        
        free_position += free_size;
    }
 
    return block_addr;
}

uintptr_t buddy_alloc_phys(buddy_allocator_t *allocator, size_t order)
{
  if (order >= MAX_ORDER)
  {
    return 0;
  }
  int current_order = order;
  while (current_order < MAX_ORDER)
  {
    buddy_section_t *section = &allocator->sections[current_order];
    if (section->block_count != 0)
    {
      uintptr_t block_addr = section->top;
      section->top = *((unsigned long*)block_addr);
      if (section->top) *((unsigned long*)section->top + 8) = 0;
      section->block_count--;

      size_t block_size = (1UL << current_order) * 4096;
      size_t bitmap_index = block_addr / block_size;
      
      bitmap_clear(section->bitmap, bitmap_index);
      if (current_order > order)
      {
        uintptr_t keep_addr = block_addr;
        size_t split_size = block_size;
        for (size_t split_order = current_order - 1; split_order + 1 > order; split_order--)
        {
          split_size /= 2;
          uintptr_t buddy_addr = keep_addr + split_size;
          buddy_section_t *buddy_section = &allocator->sections[split_order];
          add_block_to_freelist(buddy_section, buddy_addr, split_order);
        }
      }


      return block_addr;
    }
    current_order++;
  }
  return 0;
}


void* buddy_alloc_kernel(buddy_allocator_t* allocator, unsigned long virt_addr, unsigned long size)
{
    size_t actual_size = (size + 4095) & ~4095;  // Round to 4KB
    
    // Allocate exact physical size (buddy will find largest block and split down)
    uintptr_t phys_addr = buddy_alloc_phys_exact(allocator, actual_size);

    if (!phys_addr) return NULL;
    mmu_map_kernel(virt_addr, phys_addr, actual_size, 0);
  
    return (void*)virt_addr;
}

void buddy_free_kernel(buddy_allocator_t* allocator, unsigned long virt_addr, unsigned long size)
{
  uintptr_t free_position = virt_addr;
  uintptr_t block_end = virt_addr + size;
  
  while (free_position < block_end) {
      // Find the largest block size that fits in the remaining space
      size_t free_size = find_max_block_size(free_position, block_end);
      if (free_size < 4096) break;
      
      // Free this block
      size_t free_order = (63 - __builtin_clzll(free_size / 4096));

      // TODO convert virtual address to physical address before freeing hehe
      buddy_free_phys(allocator, mmu_v2p_kernel(free_position), free_order);
      
      free_position += free_size;
  }
}

void buddy_free_phys(buddy_allocator_t *allocator, uintptr_t addr, size_t order)
{
    if (order >= MAX_ORDER || addr == 0) return;

    size_t current_order = order;
    uintptr_t current_addr = addr;

    while (current_order < MAX_ORDER - 1) {
        size_t block_size = (1UL << current_order) * 4096;
        uintptr_t buddy_addr = current_addr ^ block_size;
        
        buddy_section_t *section = &allocator->sections[current_order];
        size_t buddy_bitmap_index = buddy_addr / block_size;

        if (!bitmap_test(section->bitmap, buddy_bitmap_index)) {
            break;
        }

        remove_block_from_freelist(section, buddy_addr, current_order);

        // The merged block is the LOWER address of current and buddy
        current_addr = current_addr < buddy_addr ? current_addr : buddy_addr;
        current_order++;
    }

    // Add the final coalesced block (which might be larger than original)
    buddy_section_t *final_section = &allocator->sections[current_order];
    add_block_to_freelist(final_section, current_addr, current_order);
}

unsigned long buddy_memory_available(buddy_allocator_t* allocator)
{
  unsigned long section_size = 4096;
  unsigned long memory_freed = 0;
  for (int i = 0; i < allocator->section_count; i++)
  {
    memory_freed += section_size * allocator->sections[i].block_count;
    section_size *= 2;
  }
  return memory_freed;
}
