/**
 * @file heap.h
 * @brief Kernel Dynamic Memory Allocator Interface
 * * Implements a self-bootstrapped kernel heap using an intrusive doubly-linked 
 * list layout with boundary tags. Supports variable sized allocations, 
 * in-place block resizing, contiguous block coalescing, and strict alignment parameters.
 */

#ifndef HEAP_H
#define HEAP_H

#include <type.h>

/* --- Core Configuration Constraints --- */
#define HEAP_MIN_BLOCK_SIZE  32          /**< Minimum payload footprint to prevent excessive fragmentation */
#define HEAP_MAGIC_ALLOCATED 0x414C4F43 /**< Validation tag: "ALOC" */
#define HEAP_MAGIC_FREE      0x46524545 /**< Validation tag: "FREE" */

/* --- Forward Declarations --- */
struct heap_context;

/**
 * @struct heap_block
 * @brief Intrusive boundary tag tracking individual chunk metadata.
 * * Every allocated or free memory chunk is immediately prepended by this structural header.
 * Chunks are maintained in a continuous address space, linked linearly via next/prev tokens.
 */
struct heap_block {
    u32               magic;    /**< Signature validation field checking for pointer or buffer overflows */
    bool              is_free;  /**< Flag indicating if the tracking chunk is available for scheduling */
    u64               size;     /**< Absolute payload block capacity footprint in bytes (excludes header size) */
    struct heap_block *next;    /**< Memory-contiguous subsequent sibling block pointer */
    struct heap_block *prev;    /**< Memory-contiguous antecedent sibling block pointer */
};

/**
 * @struct heap_context
 * @brief Master runtime instance block tracking an isolated heap segment.
 * * Self-bootstrapped object header located precisely at the base address of the allocated virtual region.
 */
struct heap_context {
    u64               vmm_root;   /**< Page directory level 0 physical backing token map root */
    u64               vaddr_base; /**< Start boundary memory address of the managed virtual address range */
    u64               total_size; /**< Combined capacity of the virtual region allocation in bytes */
    u64               used_size;  /**< Aggregated capacity metrics currently marked active (Headers + Payloads) */
    struct heap_block *head;      /**< Initial entry pointer anchoring the tracking block linked list */
};

/* --- Public API Operations Kernel Interfaces --- */

/**
 * @brief Allocates virtual page boundaries from the VMM and bootstraps a heap instance.
 * * @param[in]  root     Target page directory base address tracking translation properties.
 * @param[in]  sz       Desired raw capacity footprint bytes requested.
 * @param[out] out_heap Reference storage capturing the generated runtime environment handler.
 * @return 0 on success, or appropriate error token code (e.g., EINVAL, ENOMEM).
 */
int heap_create(u64 root, u64 sz, struct heap_context **out_heap);

/**
 * @brief Unmaps the entire virtual footprint of a heap instance back to the VMM.
 * * @param[in]  heap     Target context node identifier to dismantle.
 * @return 0 on success, or EINVAL if arguments fail validation bounds.
 */
int heap_destroy(struct heap_context *heap);

/**
 * @brief Allocates an unaligned sequential chunk of memory space from a target heap.
 * * Uses a First-Fit parsing strategy across available free intrusive header list ranges.
 * * @param[in]  heap     Active storage pool allocator reference node.
 * @param[in]  size     Minimum capacity requirements to yield.
 * @param[out] out_ptr  Destination reference storing the payload location address.
 * @return 0 on success, ENOMEM if space cannot be carved out, EINVAL for null handles.
 */
int heap_malloc(struct heap_context *heap, u64 size, void **out_ptr);

/**
 * @brief Marks a target tracking buffer chunk as free and optimizes memory tracking lists.
 * * Performs immediate bidirectional contiguous neighbor validation sweeps to eliminate fragmentation.
 * * @param[in]  heap     Active storage pool allocator reference node.
 * @param[in]  ptr      Payload mapping pointer target address to yield back.
 * @return 0 on success, or EINVAL if anti-corruption headers fail verification metrics.
 */
int heap_free(struct heap_context *heap, void *ptr);

/**
 * @brief Resizes, shrinks, or relocates an active payload chunk within the pool.
 * * Attempts in-place expansion via neighboring trailing blocks before resorting to migration copies.
 * * @param[in]  heap     Active storage pool allocator reference node.
 * @param[in]  ptr      Original block payload target location.
 * @param[in]  new_size New requested capacity specifications boundaries.
 * @param[out] out_ptr  Destination address output reference capturing the target pointer block.
 * @return 0 on success, or error status code on validation failure.
 */
int heap_realloc(struct heap_context *heap, void *ptr, u64 new_size, void **out_ptr);

/**
 * @brief Allocates memory structured around strict mathematical architectural power-of-two boundaries.
 * * Injects front padding blocks as needed to ensure the resulting address aligns with the specified mask.
 * * @param[in]  heap      Active storage pool allocator reference node.
 * @param[in]  alignment Structural binary mask modifier constraints (Must be power-of-two >= 16).
 * @param[in]  size      Capacity specs footprint parameters to fulfill.
 * @param[out] out_ptr   Destination storage target address tracking results.
 * @return 0 on success, EINVAL on invalid parameters, ENOMEM on allocation tracking faults.
 */
int heap_memalign(struct heap_context *heap, u64 alignment, u64 size, void **out_ptr);

/**
 * @brief Inspects tracking parameters and transfers metrics safely into diagnostic profiles.
 * * @param[in]  heap     Active storage pool allocator reference node.
 * @param[out] used     Optional output destination capturing active tracking sizes.
 * @param[out] total    Optional output destination capturing entire region properties.
 * @return 0 on success, or EINVAL if context block parameter passes null.
 */
int heap_get_stats(struct heap_context *heap, u64 *used, u64 *total);

#endif /* HEAP_H */
