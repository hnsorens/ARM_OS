#ifndef __PMM_INC_H__
#define __PMM_INC_H__


#include "pmm_driver.h"

#ifdef __MAIN__
#define GLOBAL
#define END ;
#else
#define  GLOBAL
#define END ;
#endif

 
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
GLOBAL void* (*pmm_alloc_virt_kernel)( void* vaddr, unsigned long size ) END 
 
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
GLOBAL void (*pmm_free_virt_kernel)( void* vaddr, unsigned long size ) END 
 
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
GLOBAL void* (*pmm_alloc_phys)( unsigned long order ) END 
 
  /** 
    * @brief Free previously allocated physical memory order 
    * 
    * Frees a physical memory order that was allocated with alloc_phys 
    * 
    * @param paddr Starting physical address of order (must be aligned) 
    * @param order Order of memory being freed (must be the same as allocation) 
    * 
    */ 
GLOBAL void (*pmm_free_phys)( void* paddr, unsigned long order ) END 
 
  /** 
     * @brief Get total available physical memory 
     * 
     * Returns the total amount of physical memory (in bytes) that is 
     * currently available for allocation (not currently in use). 
     * 
     * @return unsigned long Available physical memory in bytes 
     * 
     */ 
GLOBAL unsigned long (*pmm_memory_available)( void ) END 

#ifdef __MAIN__

static void pmm_fetch(core_ops *ops) {
	pmm_driver *driver = (pmm_driver*)ops->find_module_by_type(MODULE_PMM);
pmm_alloc_virt_kernel = driver->pmm->alloc_virt_kernel;
pmm_free_virt_kernel = driver->pmm->free_virt_kernel;
pmm_alloc_phys = driver->pmm->alloc_phys;
pmm_free_phys = driver->pmm->free_phys;
pmm_memory_available = driver->pmm->memory_available;
}

#endif
#endif