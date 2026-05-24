#ifndef SLAB_H
#define SLAB_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../../include/type.h"

#define SLAB_MAX_CACHES      16
#define SLAB_PAGE_SIZE       4096

/* Forward declaration of the internal slab node instance mapping */
typedef struct k_slab k_slab_t;

/* --- Isolated Object Pool Context Opaque Handle Structural Map --- */
typedef struct k_slab_cache {
    u64       root;            /* Root of page table for allocation */
    u64        obj_size;        /* Sizing requirements of the target object type */
    u64        alignment;       /* Byte boundary constraint alignments */
    u64        slots_per_slab;  /* Calculated maximum objects capacity a single page holds */
    k_slab_t     *slabs_full;      /* Head of list tracking completely populated slabs */
    k_slab_t     *slabs_partial;   /* Head of list tracking partially filled slabs */
    k_slab_t     *slabs_empty;     /* Head of list tracking unallocated, pristine slabs */
    bool          is_allocated;    /* Active descriptor presence state tracking flag */
} k_slab_cache_t;

/* --- Core Control Methods --- */

/**
 * @brief Pre-allocates and registers a brand-new fixed-size object cache store, returning a direct pointer handle.
 * @param obj_size Raw footprint sizing requirements of the target object type.
 * @param alignment Strict binary power-of-two mask constraint boundary limit.
 * @param out_cache Destination storage pointer capturing the generated direct cache handle.
 * @return int Execution confirmation status code.
 */
int k_slab_create_cache(u64 root, u64 obj_size, u64 alignment, k_slab_cache_t **out_cache);

/**
 * @brief Destroys an object cache bucket using its direct handle and drops all mapped pages back to the VMM.
 * @param cache Target direct pointer identifying the active cache instance to destroy.
 * @return int Execution confirmation status code.
 */
int k_slab_destroy_cache(k_slab_cache_t *cache);

/**
 * @brief Fetches a single pre-carved object slot from the requested slab cache using its direct handle.
 * @param cache Target direct pointer identifying the active cache instance.
 * @param out_obj Destination storage pointer capturing the generated memory allocation address.
 * @return int Execution confirmation status code.
 */
int k_slab_alloc(k_slab_cache_t *cache, void **out_obj);

/**
 * @brief Returns an active allocated object slot container back into its native cache using its direct handle.
 * @param cache Target direct pointer identifying the active cache instance.
 * @param obj Starting virtual address pointer locating the object block to free.
 * @return int Execution confirmation status code.
 */
int k_slab_free(k_slab_cache_t *cache, void *obj);

/**
 * @brief Evicts completely vacant slabs from the cache to reclaim system pages using its direct handle.
 * @param cache Target direct pointer identifying the active cache instance.
 * @return int Execution confirmation status code.
 */
int k_slab_shrink(k_slab_cache_t *cache);

#endif /* SLAB_H */
