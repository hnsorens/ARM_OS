/**
 * @file heap_api.h
 * @brief Unified Global VTable Interface for the Intrusive Boundary-Tag Heap Engine.
 *
 * Exposes a standard procedural interface managing dynamic kernel-space allocations,
 * metadata tracking structures, contiguous block optimization routines, and physical/virtual 
 * address mapping constraints via VMM integrations.
 */

#ifndef HEAP_API_H
#define HEAP_API_H

#include <type.h>

/* --- Architectural Core Magic Signatures --- */
#define HEAP_MAGIC_FREE      0x46524545         /**< String ASCII Token 'FREE' representing unallocated blocks */
#define HEAP_MAGIC_ALLOCATED 0x414C4C43         /**< String ASCII Token 'ALLC' tracking active allocations     */
#define HEAP_MIN_BLOCK_SIZE  16                 /**< Global minimum allowable block payload dimension constraint */

/* --- Opaque Structural Context Declarations --- */
struct heap_context;

/**
 * @struct heap_interface
 * @brief System operational function table for interacting with intrusive kernel memory arenas.
 *
 * Coordinates fine-grained memory management layers over page-aligned VMM virtual address spaces. 
 * Provides runtime implementations of memory layout splitting, bi-directional sweep consolidation 
 * (coalescing), in-place reallocation optimization steps, and manual boundary-tag adjustments.
 */
typedef struct heap_interface {

    /* =========================================================================
     * AREA 1: Arena Lifecycle Allocation Operators
     * ========================================================================= */

    /**
     * @brief Allocates and initializes a brand new independent memory arena context.
     *
     * Requests a large backing virtual address region via VMM hooks under fixed 
     * canonical kernel heap configurations (e.g., base `0xFFFF900000000000ULL`).
     * Instantiates an intrusive `heap_context` structure immediately followed by 
     * a monolithic root free block spanning the remaining area.
     *
     * @param[in]  root     Physical base reference address anchoring the active translation root.
     * @param[in]  sz       Desired raw capacity footprint requested for runtime use.
     * @param[out] out_heap Destination tracker pointer to capture the allocated arena header location.
     *
     * @retval 0      Success. The arena is initialized and ready for allocations.
     * @retval EINVAL Provided root reference is empty or out_heap destination address points to NULL.
     * @retval ENOMEM VMM layer failed to allocate backing physical frames or map virtual page blocks.
     */
    int (*create)(u64 root, u64 sz, struct heap_context **out_heap);

    /**
     * @brief Reclaims an entire memory arena region back to global system pools.
     *
     * Drops the entire continuous virtual memory backing layout window directly out 
     * of active virtual memory mapping frameworks in a single pass.
     *
     * @param[in] heap Target address context locating the operational arena root structure to unmap.
     *
     * @retval 0      Success. Backing storage dropped safely.
     * @retval EINVAL The passed heap parameter evaluated to NULL or lacks valid virtual address roots.
     */
    int (*destroy)(struct heap_context *heap);

    /* =========================================================================
     * AREA 2: Dynamic Core Memory Allocations
     * ========================================================================= */

    /**
     * @brief Allocates a block of memory from the arena using First-Fit search criteria.
     *
     * Normalizes the requested size to strict 16-byte alignment increments, then parses 
     * the linked list until an unallocated block satisfying the threshold is found. 
     * If the block contains excess capacity, it triggers a sub-split routine to carve off 
     * a new trailing free descriptor node.
     *
     * @param[in]  heap    Target address context locating the operational arena root structure.
     * @param[in]  size    Desired payload size parameter in bytes.
     * @param[out] out_ptr Destination pointer capturing the resulting usable payload memory address.
     *
     * @retval 0      Success; out_ptr contains the starting address of the aligned payload block.
     * @retval EINVAL Structural validation parameters evaluated to invalid states.
     * @retval ENOMEM First-Fit block search failed to locate an unallocated segment of sufficient size.
     */
    int (*malloc)(struct heap_context *heap, u64 size, void **out_ptr);

    /**
     * @brief Relinquishes an active allocation block back into unallocated structural pools.
     *
     * Extracts block metadata by walking backward exactly one header size width from the payload pointer.
     * Flips tracking signatures back to unallocated states and updates the arena's memory metrics. 
     * Immediately checks preceding and succeeding physical memory addresses to merge contiguous 
     * free blocks and eliminate fragmentation.
     *
     * @param[in] heap Target address context locating the operational arena root structure.
     * @param[in] ptr  Target address locating the active allocation payload to free.
     *
     * @retval 0      Success. Memory returned to the free pool (or no-op executed if ptr is NULL).
     * @retval EINVAL Magic signature validation failed, indicating an invalid pointer or memory corruption.
     */
    int (*free)(struct heap_context *heap, void *ptr);

    /* =========================================================================
     * AREA 3: Advanced Optimization and Alignment Modifiers
     * ========================================================================= */

    /**
     * @brief Adjusts the sizing dimensions of an existing allocation block dynamically.
     *
     * Evaluates requests across three distinct operational code paths:
     * - **Path Alpha:** Current block capacity already satisfies requirements; shrinks/splits in place.
     * - **Path Beta:** Forward neighbor block is free and contains enough total space to satisfy 
     * the expansion without moving data; consumes neighbor and splits trailing remnants.
     * - **Path Gamma:** Fallback migration. Allocates an entirely new memory block, clones old payload 
     * contents, and frees the original allocation block.
     *
     * @param[in]  heap     Target address context locating the operational arena root structure.
     * @param[in]  ptr      Starting reference address locating the allocation to modify.
     * @param[in]  new_size New capacity threshold dimensions desired.
     * @param[out] out_ptr  Destination tracker capturing the finalized valid payload target address.
     *
     * @retval 0      Success; out_ptr holds the valid address (might match or differ from original ptr).
     * @retval EINVAL Heap validation flags or block signature values matched invalid structural metrics.
     * @retval ENOMEM System failed to migrate data payload due to arena exhaustion during Path Gamma.
     */
    int (*realloc)(struct heap_context *heap, void *ptr, u64 new_size, void **out_ptr);

    /**
     * @brief Allocates an address-aligned memory block based on strict power-of-two constraints.
     *
     * Searches for unallocated blocks where the payload address can be aligned to the requested boundary. 
     * Handles alignment adjustments gracefully: if the generated padding cannot fit a minimum block header, 
     * it advances the alignment target by a chunk multiple to safely insert a valid prefix padding block descriptor.
     *
     * @param[in]  heap      Target address context locating the operational arena root structure.
     * @param[in]  alignment Target alignment boundary mask constraint. Must be a power of two >= 16.
     * @param[in]  size      Total continuous space payload requirements.
     * @param[out] out_ptr   Destination pointer capturing the strictly aligned payload address.
     *
     * @retval 0      Success. An aligned address block has been safely isolated.
     * @retval EINVAL The requested alignment is zero, or fails power-of-two structural requirements.
     * @retval ENOMEM The allocator could not find an available chunk large enough to fit the padding and size.
     */
    int (*memalign)(struct heap_context *heap, u64 alignment, u64 size, void **out_ptr);

    /* =========================================================================
     * AREA 4: Diagnostic Engineering Utilities
     * ========================================================================= */

    /**
     * @brief Samples the structural state tracking variables inside a given arena context.
     *
     * @param[in]  heap  Target address context locating the operational arena root structure.
     * @param[out] used  Destination tracking variable to record total allocated payload and header bytes.
     * @param[out] total Destination tracking variable to record total mapped backing space bytes.
     *
     * @retval 0      Success; statistics updated.
     * @retval EINVAL Target context reference address evaluated to null.
     */
    int (*get_stats)(struct heap_context *heap, u64 *used, u64 *total);

} heap_interface_t;

#endif /* HEAP_API_H */
