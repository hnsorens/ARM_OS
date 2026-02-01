#ifndef PMM_H
#define PMM_H

#include "modules/structures/pmm.h"
#include "modules/vtables/pmm.h"

#ifndef PMM
#define PMM pmm
#endif

#define EXPAND(var) var
#define CONCAT(a, b) a##b
#define CONCAT_EXPAND(a, b) CONCAT(a, b)

#ifdef __MAIN__
#define GLOBAL __attribute__((visibility("hidden")))
#define END = 0;
#else
#define GLOBAL __attribute__((visibility("hidden"))) extern 
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
GLOBAL void* (*CONCAT_EXPAND(PMM, _alloc_virt_kernel))( void* vaddr, unsigned long size ) END 
 
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
GLOBAL void (*CONCAT_EXPAND(PMM, _free_virt_kernel))( void* vaddr, unsigned long size ) END 
 
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
GLOBAL void* (*CONCAT_EXPAND(PMM, _alloc_phys))( unsigned long order ) END 
 
  /** 
    * @brief Free previously allocated physical memory order 
    * 
    * Frees a physical memory order that was allocated with alloc_phys 
    * 
    * @param paddr Starting physical address of order (must be aligned) 
    * @param order Order of memory being freed (must be the same as allocation) 
    * 
    */ 
GLOBAL void (*CONCAT_EXPAND(PMM, _free_phys))( void* paddr, unsigned long order ) END 
 
  /** 
     * @brief Get total available physical memory 
     * 
     * Returns the total amount of physical memory (in bytes) that is 
     * currently available for allocation (not currently in use). 
     * 
     * @return unsigned long Available physical memory in bytes 
     * 
     */ 
GLOBAL unsigned long (*CONCAT_EXPAND(PMM, _memory_available))( void ) END 
#ifdef __MAIN__

static void pmm_fetch(kernel_vtable_t *kvtable){
	pmm_vtable_t* module = (pmm_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_PMM);
	CONCAT_EXPAND(PMM, _alloc_virt_kernel) = module->alloc_virt_kernel;
	CONCAT_EXPAND(PMM, _free_virt_kernel) = module->free_virt_kernel;
	CONCAT_EXPAND(PMM, _alloc_phys) = module->alloc_phys;
	CONCAT_EXPAND(PMM, _free_phys) = module->free_phys;
	CONCAT_EXPAND(PMM, _memory_available) = module->memory_available;
}
#endif

#undef GLOBAL
#undef PMM

#endif