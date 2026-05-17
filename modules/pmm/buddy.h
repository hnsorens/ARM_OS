
#ifndef BUDDY_ALLOCATOR_H
#define BUDDY_ALLOCATOR_H

#include "bitmap.h"
#include "../../include/type.h"
#include "../boot_info.h"
#include <stdint.h>

#define MAX_ORDER 64

typedef void *   vaddr_t;


typedef struct page_meta
{
    uint32_t ref_count;
    uint32_t order : 8;
    uint32_t is_head : 1;
    uint32_t is_freeable : 1;
    uint32_t flags : 22;
} page_meta_t;

typedef struct page
{
    void *prev;
    void *next;
} page_t;

/**
 * @brief Represents a single order in the buddy allocator
 * 
 * Each order manages blocks of a specific size. The order determines
 * the block size as (4096 << order) bytes.
 */
typedef struct buddy_section_t
{
  bitmap_t* bitmap;
  page_t *top;
  size_t block_count;
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
void buddy_init(memory_region_t* memory_map, size_t region_count);

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
k_status_t buddy_alloc_page(uint8_t order, phys_addr_t *dest);

/**
 * @brief Free previously allocated physical memory block
 * 
 * Returns block to free list and coalesces with buddies if possible.
 * 
 * @param allocator Initialized buddy allocator
 * @param addr Physical address of block to free
 * @param order Order of the block being freed
 */
k_status_t buddy_free_page(uint8_t order, phys_addr_t addr);

/**
 * @brief Allocate kernel virtual memory
 * 
 * Allocates physical memory and maps it to kernel virtual address space.
 * 
 * @param allocator Initialized buddy allocator
 * @param virt_addr Virtual address to map to
 * @param size Number of bytes to allocate
 * @return Virtual address of allocated memory, or NULL if failed
 */
void* buddy_alloc_kernel(vaddr_t virt_addr, size_t size);

/**
 * @brief Free kernel virtual memory
 * 
 * Unmaps virtual memory and frees underlying physical memory.
 * 
 * @param allocator Initialized buddy allocator
 * @param virt_addr Virtual address to free
 * @param size Number of bytes to free
 */
void buddy_free_kernel(vaddr_t virt_addr, size_t size);

/**
 * @brief Get total available physical memory
 * 
 * @param allocator Initialized buddy allocator
 * @return Total bytes of free physical memory
 */
unsigned long buddy_memory_available();

#endif

