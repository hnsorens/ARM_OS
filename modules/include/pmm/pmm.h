#ifndef PMM_VTABLE_H
#define PMM_VTABLE_H


#include "pmm_types.h"

/**
 * @brief Physical Memory Manager (PMM) VTable
 * 
 * Manages physical memory allocation and tracking for the kernel.
 * Responsible for allocating/freeing physical pages and tracking
 * available physical memory.
 */
typedef struct pmm_ops
{

  /**
    * @brief Allocate contiguous virtual memory in kernel space
    * 
    * Allocates a region of physical memory and places it in the kernel memory map.
    * This does not keep track of virtual memory allocations, this is only for placing
    * physical allocations into a spot in virtual memory.
    * 
    * @param vaddr Virual address in page table to map memory region to
    * @param size Size of the region to allocate in bytes
    * @return void* Starting virtual address of allocated region, or NULL on failure
    * 
    */
  void* (*alloc_virt_kernel)(void* vaddr, unsigned long size);

  /**
    * @brief Free previously allocated kernel virtual memory
    * 
    * Releases a region of physical address space that was placed in the
    * kernel's page table.
    * 
    * @param vaddr Starting virtual address of region to free
    * @param size Size of the region in bytes (must match allocation size)
    * 
    */
  void (*free_virt_kernel)(void* vaddr, unsigned long size);

  /**
    * @brief Allocate an order of physical memory
    * 
    * Allocates a single order of physical memory given the order
    * (power of 2 starting at 1 page (4kb))
    * 
    * @param order Memory Chunk Order (power of 2, 0 being (4kb))
    * @return void* Physical address of first allocated chunk, or NULL on failure
    * 
    */
  void* (*alloc_phys)(unsigned long order);

  /**
    * @brief Free previously allocated physical memory order
    * 
    * Frees a physical memory order that was allocated with alloc_phys
    * 
    * @param paddr Starting physical address of order (must be aligned)
    * @param order Order of memory being freed (must be the same as allocation)
    * 
    */
  void (*free_phys)(void* paddr, unsigned long order);

  /**
     * @brief Get total available physical memory
     * 
     * Returns the total amount of physical memory (in bytes) that is
     * currently available for allocation (not currently in use).
     * 
     * @return unsigned long Available physical memory in bytes
     * 
     */
  unsigned long (*memory_available)();
} pmm_ops;

#endif
