#include "buddy_allocator.h"
#include "Uefi/UefiMultiPhase.h"
#include "Uefi/UefiPxe.h"
#include "Uefi/UefiSpec.h"
#include "data_structures/bitmap.h"
#include <stdint.h>

size_t buddy_get_memory_size(size_t total_memory)
{
  size_t buddy_size = 0;
  size_t block_size = 4096;

  while (block_size <= total_memory)
  {
    size_t block_count = (total_memory / block_size) + 1;
    buddy_size += bitmap_memory_size(block_count);

    // Double the block size for next level
    if (block_size > total_memory / 2) break;
    block_size *= 2;
  }

  return buddy_size;
}

uint64_t find_max_block(uintptr_t addr) {
  uint64_t max_possible = 63 - __builtin_clzll(addr);

  while (max_possible > 0)
  {
    if ((addr & (max_possible - 1)) == 0)
    {
      return max_possible;
    }
    max_possible >>= 1;
  }
  return 0;
}

void buddy_init(EFI_MEMORY_DESCRIPTOR* memory_map, size_t region_count, buddy_allocator_t *allocator, uintptr_t buddy_memory, size_t total_memory)
{
  size_t block_size = 4096;
  uintptr_t buddy_memory_pos = buddy_memory;
  uint32_t order = 0;

  while (block_size <= total_memory)
  {
    size_t block_count = (total_memory / block_size) + 1;
    allocator->sections[order].bitmap = bitmap_create(buddy_memory_pos, block_count);

    if (block_size > total_memory / 2) break;
    block_size *= 2;
    buddy_memory_pos += bitmap_memory_size(block_count);
  }

  for (int i = 0; i < region_count; i++)
  {
    if (memory_map[i].Type == EfiConventionalMemory)
    {
      uintptr_t block_end = memory_map[i].PhysicalStart + memory_map[i].NumberOfPages * 4096;
      uintptr_t block_position = memory_map[i].PhysicalStart;
      while (block_position < block_end)
      {
        int block_size = find_max_block(block_position);
        while (block_position + block_size > block_end)
        {
          block_size /= 2;
          if (block_size < 4096) break;
        }
        if (block_size < 4096) break;
        // handle free
        uint64_t section_index = (63 - __builtin_clzll(block_size / 4096));
        uint64_t page_index = block_position / block_size;
        bitmap_set(allocator->sections[section_index].bitmap, page_index);
        *((uintptr_t*)block_position) = allocator->sections[section_index].top;
        allocator->sections[section_index].top = block_position;
        block_position += block_size;
      }
    }
  }
}
