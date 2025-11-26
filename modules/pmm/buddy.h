
#ifndef BUDDY_ALLOCATOR_H
#define BUDDY_ALLOCATOR_H

#include "bitmap.h"
#include "../module_vtables.h"
#include <stddef.h>

#define MAX_ORDER 64

typedef unsigned long   buddy_block_addr_t;
typedef unsigned long   buddy_order_t;
typedef unsigned long   buddy_block_count_t;
typedef unsigned long   buddy_memory_size_t;
typedef unsigned long   vaddr_t;

/**
 * @brief Represents a single order in the buddy allocator
 * 
 * Each order manages blocks of a specific size. The order determines
 * the block size as (4096 << order) bytes.
 */
typedef struct buddy_section_t
{
  bitmap_t* bitmap;
  buddy_block_addr_t top;
  buddy_block_count_t block_count;
} buddy_section_t;

/**
 * @brief Main buddy allocator structure
 * 
 * Manages memory using the buddy allocation algorithm with multiple orders.
 * Each order handles blocks of progressively larger power-of-2 sizes.
 */
typedef struct buddy_allocator_t
{
  buddy_section_t sections[MAX_ORDER];
  unsigned long section_count;
} buddy_allocator_t;


/**
 * @brief Initialize buddy allocator with available memory regions
 * 
 * Sets up the allocator structure and populates free lists with available
 * memory from the memory map.
 * 
 * @param memory_map Array of memory regions from bootloader
 * @param region_count Number of regions in memory_map
 * @param allocator Allocator structure to initialize
 * @param buddy_memory Pre-allocated memory for buddy metadata
 * @param total_memory Total physical memory size
 */
void buddy_init(memory_region_t* memory_map, size_t region_count, 
        buddy_allocator_t *allocator, uintptr_t buddy_memory,
        buddy_memory_size_t total_memory);

/**
 * @brief Calculate memory needed for buddy allocator metadata
 * 
 * @param total_memory Total physical memory to manage
 * @return Bytes required for buddy allocator bitmaps and structures
 */
size_t buddy_get_memory_size(size_t total_memory);

/**
 * @brief Allocate physical memory block of specific order
 * 
 * Allocates a block of size (4096 << order) bytes.
 * 
 * @param allocator Initialized buddy allocator
 * @param order Block order (0 = 4KB, 1 = 8KB, etc.)
 * @return Physical address of allocated block, or 0 if failed
 */
uintptr_t buddy_alloc_phys(buddy_allocator_t *allocator, buddy_order_t order);

/**
 * @brief Free previously allocated physical memory block
 * 
 * Returns block to free list and coalesces with buddies if possible.
 * 
 * @param allocator Initialized buddy allocator
 * @param addr Physical address of block to free
 * @param order Order of the block being freed
 */
void buddy_free_phys(buddy_allocator_t *allocator, buddy_block_addr_t addr, buddy_order_t order);

/**
 * @brief Allocate kernel virtual memory
 * 
 * Allocates physical memory and maps it to kernel virtual address space.
 * 
 * @param allocator Initialized buddy allocator
 * @param virt_addr Virtual address to map to
 * @param size Number of bytes to allocate
 * @param vmm Virtual memory manager instance
 * @return Virtual address of allocated memory, or NULL if failed
 */
void* buddy_alloc_kernel(buddy_allocator_t* allocator, vaddr_t virt_addr, buddy_memory_size_t size, vmm_vtable_t* vmm);

/**
 * @brief Free kernel virtual memory
 * 
 * Unmaps virtual memory and frees underlying physical memory.
 * 
 * @param allocator Initialized buddy allocator
 * @param virt_addr Virtual address to free
 * @param size Number of bytes to free
 * @param vmm Virtual memory manager instance
 */
void buddy_free_kernel(buddy_allocator_t* allocator, vaddr_t virt_addr, buddy_memory_size_t size, vmm_vtable_t* vmm);

/**
 * @brief Get total available physical memory
 * 
 * @param allocator Initialized buddy allocator
 * @return Total bytes of free physical memory
 */
unsigned long buddy_memory_available(buddy_allocator_t* allocator);

#endif
