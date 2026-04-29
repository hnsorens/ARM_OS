#ifndef __PMM_INC_H__
#define __PMM_INC_H__


#include "pmm_types.h"

#ifndef CONCAT_HIDDEN
#define CONCAT_HIDDEN(a, b) a ## b
#define CONCAT(a, b) CONCAT_HIDDEN(a, b)
#endif

#define pmm_alloc_virt_kernel CONCAT(IMPL_NAME, _alloc_virt_kernel_func)
#define pmm_free_virt_kernel CONCAT(IMPL_NAME, _free_virt_kernel_func)
#define pmm_alloc_phys CONCAT(IMPL_NAME, _alloc_phys_func)
#define pmm_free_phys CONCAT(IMPL_NAME, _free_phys_func)
#define pmm_memory_available CONCAT(IMPL_NAME, _memory_available_func)

 
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
__attribute__((used)) void* pmm_alloc_virt_kernel( void* vaddr, unsigned long size );
 
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
__attribute__((used)) void pmm_free_virt_kernel( void* vaddr, unsigned long size );
 
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
__attribute__((used)) void* pmm_alloc_phys( unsigned long order );
 
  /** 
    * @brief Free previously allocated physical memory order 
    * 
    * Frees a physical memory order that was allocated with alloc_phys 
    * 
    * @param paddr Starting physical address of order (must be aligned) 
    * @param order Order of memory being freed (must be the same as allocation) 
    * 
    */ 
__attribute__((used)) void pmm_free_phys( void* paddr, unsigned long order );
 
  /** 
     * @brief Get total available physical memory 
     * 
     * Returns the total amount of physical memory (in bytes) that is 
     * currently available for allocation (not currently in use). 
     * 
     * @return unsigned long Available physical memory in bytes 
     * 
     */ 
__attribute__((used)) unsigned long pmm_memory_available( void );
typedef void (*init_fn_t)(void);
#define __init_func __attribute__((section(".init_array"), used))
#define MODULE_INIT(func) \
static init_fn_t __init_ptr##func __init_func = func;
#endif