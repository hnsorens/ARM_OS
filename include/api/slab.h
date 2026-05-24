#ifndef SLAB_ALLOCATOR_API_H
#define SLAB_ALLOCATOR_API_H

#include "../type.h"

struct k_slab_cache;

/* --- Unified Object-Oriented Slab Allocator Module Interface --- */
typedef struct slab_interface {
    /**
     * @brief Creates a new isolated memory pool for a specific object size.
     * @param root Root of page table used to allocate memory in the vmm
     * @param obj_size Raw footprint sizing requirements of the target object type.
     * @param alignment Strict binary power-of-two mask constraint boundary limit.
     * @param out_cache Destination storage pointer capturing the generated direct cache handle.
     * @return int Execution confirmation status code.
     */
    int (*create_cache)(u64 root, u64 obj_size, u64 alignment, struct k_slab_cache **out_cache);

    /**
     * @brief Destroys an active cache instance and returns all backed pages to the VMM.
     * @param cache Target direct pointer identifying the active cache instance to destroy.
     * @return int Execution confirmation status code.
     */
    int (*destroy_cache)(struct k_slab_cache *cache);

    /**
     * @brief Grabs one object-sized slot directly out of the requested cache context.
     * @param cache Target direct pointer identifying the active cache instance.
     * @param out_obj Destination storage pointer capturing the generated memory allocation address.
     * @return int Execution confirmation status code.
     */
    int (*alloc)(struct k_slab_cache *cache, void **out_obj);

    /**
     * @brief Returns an active allocated slot container back into its native cache context.
     * @param cache Target direct pointer identifying the active cache instance.
     * @param obj Starting virtual address pointer locating the object block to free.
     * @return int Execution confirmation status code.
     */
    int (*free)(struct k_slab_cache *cache, void *obj);

    /**
     * @brief Reclaims unused, pristine empty slabs if the system is running low on memory frames.
     * @param cache Target direct pointer identifying the active cache instance.
     * @return int Execution confirmation status code.
     */
    int (*shrink)(struct k_slab_cache *cache);
} slab_interface_t;

#endif /* SLAB_ALLOCATOR_API_H */
