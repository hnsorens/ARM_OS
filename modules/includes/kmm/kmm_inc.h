#ifndef __KMM_INC_H__
#define __KMM_INC_H__


#include "kmm_driver.h"

#ifdef __MAIN__
#define GLOBAL __attribute__((visibility("hidden")))
#define END = 0;
#else
#define GLOBAL __attribute__((visibility("hidden"))) extern
#define END ;
#endif

  /** 
    * @brief Allocate memory from kernel heap 
    * 
    * Allocates a contiguous block of memory from the kernel's heap. 
    * Memory is not initialized (contains garbage data). 
    * 
    * @param size Number of bytes to allocate 
    * @return void* Pointer to allocated memory, or NULL on failure 
    * 
    */ 
GLOBAL void* (*kmm_kmalloc)( unsigned long size ) END 
 
  /** 
    * @brief Allocate aligned memory from kernel heap 
    * 
    * Allocates memory with specific alignment requirement. 
    * Useful for DMA buffers or hardware that requires specific alignment. 
    * 
    * @param size Number of bytes to allocate 
    * @param align Alignment requirement (must be power of 2) 
    * @return void* Pointer to allocated aligned memory, or NULL on failure 
    * 
    */ 
GLOBAL void* (*kmm_kmalloc_aligned)( unsigned long size, unsigned long align ) END 
 
  /** 
    * @brief Allocate and zero-initialize memory 
    * 
    * Allocates memory and sets all bytes to zero. 
    * Equivalent to kmalloc + memset(0). 
    * 
    * @param count Number of elements to allocate 
    * @param size Size of each element in bytes 
    * @return void* Pointer to zero-initialized memory, or NULL on failure 
    * 
    */ 
GLOBAL void* (*kmm_kcalloc)( unsigned long count, unsigned long size ) END 
 
  /** 
    * @brief Reallocate memory block 
    * 
    * Changes the size of a previously allocated memory block. 
    * Contents are preserved up to the minimum of old and new size. 
    * 
    * @param ptr Pointer to previously allocated memory (or NULL) 
    * @param size New size in bytes 
    * @return void* Pointer to reallocated memory, or NULL on failure 
    * 
    * Implementation Notes: 
    * - If ptr is NULL, equivalent to kmalloc(size) 
    * - If size is 0 and ptr not NULL, equivalent to kfree(ptr) 
    * - May move memory to new location if can't expand in place 
    * - Preserve data from old location up to min(old_size, new_size) 
    */ 
GLOBAL void* (*kmm_krealloc)( void* ptr, unsigned long size ) END 
 
  /** 
     * @brief Free previously allocated kernel memory 
     * 
     * Returns memory to the kernel heap for reuse. 
     * Pointer must have been returned by kmalloc, kcalloc, or krealloc. 
     * 
     * @param ptr Pointer to memory to free (can be NULL) 
     * 
     */ 
GLOBAL void (*kmm_kfree)( void* ptr ) END 
 
  /** 
   * @brief Allocates memory from slab allocator 
   * 
   * Memory chunks in slab allocators have a size of a power of 2, 
   * which is below a page size (4096 bytes). For allocating above 
   * 4096 bytes, use the physical memory manager. 
   * 
   * @param order Order of memory allocation (< 12) 
   * 
   * @return Physical memory address 
   */ 
GLOBAL void* (*kmm_ksalloc)( int order ) END 
 
  /** 
   * @brief Frees memory allocated from slab allocator 
   * 
   * Memory and order must be the same as order and address in 
   * allocation of memory 
   * 
   * @param order Order of memory allocated 
   * @param addr Address of memory allocated 
   */ 
GLOBAL void (*kmm_ksfree)( int order, void* addr ) END 

#ifdef __MAIN__

static void kmm_fetch(core_ops *ops) {
	kmm_driver *driver = (kmm_driver*)ops->find_module_by_type(MODULE_KMM);
kmm_kmalloc = driver->kmm->kmalloc;
kmm_kmalloc_aligned = driver->kmm->kmalloc_aligned;
kmm_kcalloc = driver->kmm->kcalloc;
kmm_krealloc = driver->kmm->krealloc;
kmm_kfree = driver->kmm->kfree;
kmm_ksalloc = driver->kmm->ksalloc;
kmm_ksfree = driver->kmm->ksfree;
}

#endif
#endif