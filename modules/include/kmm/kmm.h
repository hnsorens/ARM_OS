#ifndef KMM_VTABLE_H
#define KMM_VTABLE_H


#include "kmm_types.h"

/**
 * @brief Kernel Memory Manager (KMM) VTable
 * 
 * Provides dynamic memory allocation for kernel objects and data
 * structures. Manages kernel heap memory with various allocation
 * strategies.
 */
typedef struct kmm_ops
{
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
  void* (*kmalloc)(unsigned long size);

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
  void* (*kmalloc_aligned)(unsigned long size, unsigned long align);

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
  void* (*kcalloc)(unsigned long count, unsigned long size);

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
  void* (*krealloc)(void* ptr, unsigned long size);

  /**
     * @brief Free previously allocated kernel memory
     * 
     * Returns memory to the kernel heap for reuse.
     * Pointer must have been returned by kmalloc, kcalloc, or krealloc.
     * 
     * @param ptr Pointer to memory to free (can be NULL)
     * 
     */
  void (*kfree)(void *ptr);

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
  void *(*ksalloc)(int order);

  /**
   * @brief Frees memory allocated from slab allocator
   *
   * Memory and order must be the same as order and address in
   * allocation of memory
   *
   * @param order Order of memory allocated
   * @param addr Address of memory allocated
   */
  void (*ksfree)(int order, void *addr);
  
} kmm_ops;

#endif