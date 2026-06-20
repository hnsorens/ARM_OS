/**
 * @file slab.h
 * @brief Public Interface Definition for the Kernel Fixed-Size Slab Allocator Subsystem.
 * * Exposes fixed-size element tracking cache interfaces providing accelerated object mapping pools.
 * Manages allocation routing across explicit tri-state tracking lists (empty, partial, full) to achieve 
 * optimized page usage and zero spatial internal fragmentation.
 */

#ifndef SLAB_H
#define SLAB_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <type.h>

#define SLAB_MAX_CACHES      16     /**< Maximum distinct cache buckets allowed throughout system runtime modules */
#define SLAB_PAGE_SIZE       4096   /**< Standard architectural layout virtual page size boundary requirement */

/* Forward declaration of the internal slab node tracking instance */
typedef struct k_slab k_slab_t;

/**
 * @struct k_slab_cache_t
 * @brief Master runtime instance context block organizing a unique, fixed-size allocation pool.
 * * Opaque descriptor organizing execution details and tracking lists across distinct sub-pools.
 */
typedef struct k_slab_cache {
    u64       root;           /**< Page directory base address physical tracking translation table root */
    u64        obj_size;        /**< Custom fixed cell size capacity parameter footprint (includes padding) */
    u64        alignment;       /**< Mathematical alignment rule constraints enforced on allocated cells */
    u64        slots_per_slab;  /**< Maximum object capacity threshold a single physical page can accommodate */
    k_slab_t     *slabs_full;      /**< Link list head tracking completely populated backing pages */
    k_slab_t     *slabs_partial;   /**< Link list head tracking partially occupied backing pages */
    k_slab_t     *slabs_empty;     /**< Link list head tracking vacant, unallocated backing pages available for reuse */
    bool          is_allocated;    /**< Active flag indicating if descriptor node properties stand valid */
} k_slab_cache_t;

/* --- Public API Operations Kernel Interfaces --- */

/**
 * @brief Allocates virtual pages from the VMM to build and boot an independent fixed-size cache sub-pool.
 * @param[in]  root Target page directory tracking translation property level-0 table physical root.
 * @param[in]  obj_size Raw capacity footprint sizing requirements of target objects.
 * @param[in]  alignment Strict power-of-two alignment mask requirements (Must be >= 1).
 * @param[out] out_cache Target reference capturing the generated cache runtime environment handle.
 * @return 0 on success, or appropriate error token code (e.g., EINVAL, ENOMEM).
 */
int k_slab_create_cache(u64 root, u64 obj_size, u64 alignment, k_slab_cache_t **out_cache);

/**
 * @brief Tears down a targeted object cache, returning all tracked backing pages directly back to the VMM.
 * @param[in] cache Target direct pointer identifying the active cache instance to dismantle.
 * @return 0 on success, or EINVAL if arguments fail validation bounds checks.
 */
int k_slab_destroy_cache(k_slab_cache_t *cache);

/**
 * @brief Fetches a pre-carved, optimally aligned object cell allocation from the requested slab pool.
 * Evaluates partial queues first, falling back to empty pools or expanding new pages as needed.
 * @param[in]  cache Target direct pointer tracking the active cache pool instance to query.
 * @param[out] out_obj Destination reference mapping store capturing the resulting cell memory address.
 * @return 0 on success, ENOMEM if infrastructure limits are hit, EINVAL on invalid parameter blocks.
 */
int k_slab_alloc(k_slab_cache_t *cache, void **out_obj);

/**
 * @brief Returns an active memory cell back to its native parent slab pool, reclaiming the slot for future reuse.
 * Triggers bidirectional sorting metrics to reorganize pages across tracking lists.
 * @param[in] cache Target direct pointer tracking the active cache pool instance.
 * @param[in] obj Starting virtual address pointer locating the active object cell block to free.
 * @return 0 on success, or EINVAL if target address configurations cross out of valid boundary rules.
 */
int k_slab_free(k_slab_cache_t *cache, void *obj);

/**
 * @brief Iterates down the empty list queue, unmapping completely vacant pages back to core storage systems.
 * @param[in] cache Target direct pointer tracking the active cache pool instance to sweep.
 * @return 0 on success, or EINVAL if handle fails active validation requirements.
 */
int k_slab_shrink(k_slab_cache_t *cache);

#endif /* SLAB_H */
