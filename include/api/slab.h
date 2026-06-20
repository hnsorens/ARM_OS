/**
 * @file slab_allocator_api.h
 * @brief Unified Global VTable Interface for the Fixed-Size Slab Allocator Subsystem.
 *
 * Exposes a structured, object-oriented function table interface to allow kernel
 * modules and system tasks to interact with the slab memory allocation pools.
 */

#ifndef SLAB_ALLOCATOR_API_H
#define SLAB_ALLOCATOR_API_H

#include <type.h>

/* Forward declaration to hide internal layout properties from API consumers */
struct k_slab_cache;

/**
 * @struct slab_interface
 * @brief Object-oriented operational dispatch table for managing slab caches.
 *
 * Provides a clean abstraction boundary over the underlying page tracking structures,
 * isolating consumers from implementation details like intrusive linking or state queues.
 */
typedef struct slab_interface {
    /**
     * @brief Creates a new isolated memory pool for a specific object size.
     *
     * Automatically adjusts the internal runtime object footprint size based on alignment 
     * masks and the 4-byte minimum size floor required for the intrusive freelist links. 
     * Allocates a base descriptor control block through the Virtual Memory Manager (VMM).
     *
     * @param[in]  root       Page directory base physical root tracking the execution context.
     * @param[in]  obj_size   Raw unaligned sizing footprint requirements for the target object.
     * @param[in]  alignment  Strict binary power-of-two alignment mask boundary factor.
     * @param[out] out_cache  Destination handle to store the newly generated opaque cache pointer.
     *
     * @retval 0      Success. The cache is initialized and available for immediate allocations.
     * @retval EINVAL Invalid parameter (e.g., size/alignment is 0, alignment is not a power-of-two,
     * out_cache is NULL, or the object size exceeds the capacity of a standard system page).
     * @retval ENOMEM Subsystem failed to allocate virtual pages via the VMM to establish the control header.
     */
    int (*create_cache)(u64 root, u64 obj_size, u64 alignment, struct k_slab_cache **out_cache);

    /**
     * @brief Destroys an active cache instance and returns all backed pages to the VMM.
     *
     * Iterates systematically down the tracking streams (full, partial, empty), completely 
     * unmapping all underlying page layers. Once the tracking pools are fully evicted, the 
     * parent control cache handle is dropped from virtual memory.
     *
     * @note Crucial constraint: All active objects previously parsed out of this pool are
     * invalidated instantly upon invocation. Forcing teardown with dangling pointers causes panics.
     *
     * @param[in] cache Target direct pointer identifying the active cache instance to dismantle.
     *
     * @retval 0      Success. All internal pages and descriptor frameworks have been released.
     * @retval EINVAL Passed handle is NULL, or references an inactive descriptor pool (`is_allocated == false`).
     */
    int (*destroy_cache)(struct k_slab_cache *cache);

    /**
     * @brief Grabs one object-sized slot directly out of the requested cache context.
     *
     * Executes in strict $O(1)$ constant time. Evaluates partial queues first to optimize 
     * spatial density, falling back to empty pools, or requesting physical page expansions 
     * from the virtual memory manager if current capacity bounds are entirely saturated.
     *
     * @param[in]  cache    Target direct pointer tracking the active cache pool instance to query.
     * @param[out] out_obj  Destination reference mapping store capturing the resulting slot address.
     *
     * @retval 0      Success. Memory allocation is successfully prepared and written to out_obj.
     * @retval EINVAL Passed handle parameters are NULL, or the targeted cache pool is marked inactive.
     * @retval ENOMEM Resource exhaustion; underlying memory systems cannot fulfill physical page expansion.
     */
    int (*alloc)(struct k_slab_cache *cache, void **out_obj);

    /**
     * @brief Returns an active allocated slot container back into its native cache context.
     *
     * Recycles the target memory address using Last-In-First-Out (LIFO) behavioral rules, 
     * allowing immediate reuse on the next allocation path. Triggers bidirectional sorting 
     * passes to shift the owning page across state tracking lists (full -> partial -> empty).
     *
     * @note Memory addresses are automatically aligned down to calculate the intrusive page structure,
     * allowing the allocator to protect against boundary-crossing or corrupted pointers.
     *
     * @param[in] cache Target direct pointer tracking the active cache pool instance.
     * @param[in] obj   Starting virtual address pointer locating the active object cell block to free.
     *
     * @retval 0      Success. The block has been safely unlinked and placed back into the freelist stack.
     * @retval EINVAL Passed handle parameters are NULL, or the targeted object address falls outside the 
     * calculated tracking limits, boundaries, or alignment rules of the target page frame.
     */
    int (*free)(struct k_slab_cache *cache, void *obj);

    /**
     * @brief Reclaims unused, pristine empty slabs if the system is running low on memory frames.
     *
     * Sweeps the `slabs_empty` tracking list context of the targeted cache pool, unlinking 
     * unutilized, formatted page frames and transferring them directly back to core kernel allocation 
     * modules without fracturing active data contexts.
     *
     * @param[in] cache Target direct pointer tracking the active cache pool instance to sweep.
     *
     * @retval 0      Success. All completely vacant memory layers have been scrubbed and returned to the VMM.
     * @retval EINVAL Passed handle is NULL, or references an inactive descriptor pool.
     */
    int (*shrink)(struct k_slab_cache *cache);
} slab_interface_t;

#endif /* SLAB_ALLOCATOR_API_H */
