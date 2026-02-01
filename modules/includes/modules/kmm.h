#ifndef KMM_H
#define KMM_H

#include "modules/structures/kmm.h"
#include "modules/vtables/kmm.h"

#ifndef KMM
#define KMM kmm
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
    * @brief Allocate memory from kernel heap 
    * 
    * Allocates a contiguous block of memory from the kernel's heap. 
    * Memory is not initialized (contains garbage data). 
    * 
    * @param size Number of bytes to allocate 
    * @return void* Pointer to allocated memory, or NULL on failure 
    * 
    */ 
GLOBAL void* (*CONCAT_EXPAND(KMM, _kmalloc))( unsigned long size ) END 
 
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
GLOBAL void* (*CONCAT_EXPAND(KMM, _kmalloc_aligned))( unsigned long size, unsigned long align ) END 
 
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
GLOBAL void* (*CONCAT_EXPAND(KMM, _kcalloc))( unsigned long count, unsigned long size ) END 
 
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
GLOBAL void* (*CONCAT_EXPAND(KMM, _krealloc))( void* ptr, unsigned long size ) END 
 
  /** 
     * @brief Free previously allocated kernel memory 
     * 
     * Returns memory to the kernel heap for reuse. 
     * Pointer must have been returned by kmalloc, kcalloc, or krealloc. 
     * 
     * @param ptr Pointer to memory to free (can be NULL) 
     * 
     */ 
GLOBAL void (*CONCAT_EXPAND(KMM, _kfree))( void* ptr ) END 
#ifdef __MAIN__

static void kmm_fetch(kernel_vtable_t *kvtable){
	kmm_vtable_t* module = (kmm_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_KMM);
	CONCAT_EXPAND(KMM, _kmalloc) = module->kmalloc;
	CONCAT_EXPAND(KMM, _kmalloc_aligned) = module->kmalloc_aligned;
	CONCAT_EXPAND(KMM, _kcalloc) = module->kcalloc;
	CONCAT_EXPAND(KMM, _krealloc) = module->krealloc;
	CONCAT_EXPAND(KMM, _kfree) = module->kfree;
}
#endif

#undef GLOBAL
#undef KMM

#endif