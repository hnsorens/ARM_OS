#ifndef __KMM_INC_H__
#define __KMM_INC_H__


#include "kmm_types.h"

#ifndef CONCAT_HIDDEN
#define CONCAT_HIDDEN(a, b) a ## b
#define CONCAT(a, b) CONCAT_HIDDEN(a, b)
#endif

#define kmm_kmalloc CONCAT(IMPL_NAME, _kmalloc_func)
#define kmm_kmalloc_aligned CONCAT(IMPL_NAME, _kmalloc_aligned_func)
#define kmm_kcalloc CONCAT(IMPL_NAME, _kcalloc_func)
#define kmm_krealloc CONCAT(IMPL_NAME, _krealloc_func)
#define kmm_kfree CONCAT(IMPL_NAME, _kfree_func)
#define kmm_ksalloc CONCAT(IMPL_NAME, _ksalloc_func)
#define kmm_ksfree CONCAT(IMPL_NAME, _ksfree_func)

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
__attribute__((used)) void* kmm_kmalloc( unsigned long size );
 
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
__attribute__((used)) void* kmm_kmalloc_aligned( unsigned long size, unsigned long align );
 
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
__attribute__((used)) void* kmm_kcalloc( unsigned long count, unsigned long size );
 
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
__attribute__((used)) void* kmm_krealloc( void* ptr, unsigned long size );
 
  /** 
     * @brief Free previously allocated kernel memory 
     * 
     * Returns memory to the kernel heap for reuse. 
     * Pointer must have been returned by kmalloc, kcalloc, or krealloc. 
     * 
     * @param ptr Pointer to memory to free (can be NULL) 
     * 
     */ 
__attribute__((used)) void kmm_kfree( void* ptr );
 
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
__attribute__((used)) void* kmm_ksalloc( int order );
 
  /** 
   * @brief Frees memory allocated from slab allocator 
   * 
   * Memory and order must be the same as order and address in 
   * allocation of memory 
   * 
   * @param order Order of memory allocated 
   * @param addr Address of memory allocated 
   */ 
__attribute__((used)) void kmm_ksfree( int order, void* addr );
typedef void (*init_fn_t)(void);
#define __init_func __attribute__((section(".init_array"), used))
#define MODULE_INIT(func) \
static init_fn_t __init_ptr##func __init_func = func;
#endif