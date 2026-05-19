#ifndef HEAP_H
#define HEAP_H

#include "../../include/type.h"

#define HEAP_MIN_BLOCK_SIZE  32
#define HEAP_MAGIC_ALLOCATED 0x414C4F43 /* "ALOC" */
#define HEAP_MAGIC_FREE      0x46524545 /* "FREE" */

/* --- Forward Declarations --- */
typedef struct heap_context heap_context_t;

/* --- Boundary Tag Header Tracking Individual Block Metadata --- */
typedef struct heap_block {
    uint32_t           magic;         /* Verification tag checking corruption */
    bool               is_free;       /* Allocation tracking status flag */
    size_t             size;          /* Total payload block size footprint in bytes */
    struct heap_block *next;          /* Single forward sibling block pointer */
    struct heap_block *prev;          /* Single reverse sibling block pointer */
} heap_block_t;

/**
 * @brief Dynamically allocates virtual memory space via VMM and initializes a new heap instance.
 * @param root Target level 0 page descriptor mapping structure frame.
 * @param sz Initial continuous backing size requested for the heap instance.
 * @param out_heap Destination storage pointer capturing the generated heap context handle.
 * @return k_status_t Execution confirmation code.
 */
k_status_t heap_create(paddr_t root, size_t sz, heap_context_t **out_heap);

/**
 * @brief Completely dismantles a heap instance, releasing its virtual memory range back to the VMM.
 * @param heap Target handle identifying the active heap context instance to destroy.
 * @return k_status_t Execution confirmation code.
 */
k_status_t heap_destroy(heap_context_t *heap);

/**
 * @brief Allocates an arbitrary sequential chunk of byte-level payload space from a specific heap.
 * @param heap Target handle identifying the active heap context instance.
 * @param size Requested capacity footprint limits in bytes.
 * @param out_ptr Destination storage pointer capturing the generated memory allocation address.
 * @return k_status_t Execution confirmation code.
 */
k_status_t heap_malloc(heap_context_t *heap, size_t size, void **out_ptr);

/**
 * @brief Evaluates validation context maps and returns blocks back into active target pools.
 * @param heap Target handle identifying the active heap context instance.
 * @param ptr Target starting payload block location coordinates to free.
 * @return k_status_t Execution confirmation code.
 */
k_status_t heap_free(heap_context_t *heap, void *ptr);

/**
 * @brief Adjusts, migrates, or expands active sequential blocks safely within a specific heap.
 * @param heap Target handle identifying the active heap context instance.
 * @param ptr Target location anchoring current tracked operations blocks.
 * @param new_size Scale modification tracking limits.
 * @param out_ptr Destination storage pointer capturing the adjusted or migrated memory address.
 * @return k_status_t Execution confirmation code.
 */
k_status_t heap_realloc(heap_context_t *heap, void *ptr, size_t new_size, void **out_ptr);

/**
 * @brief Spawns custom byte alignments tracking specific architectural boundaries from a heap.
 * @param heap Target handle identifying the active heap context instance.
 * @param alignment Strict binary power-of-two mask constraint bounds.
 * @param size Requested storage sizing dimensions tracking allocations footprint.
 * @param out_ptr Destination storage pointer capturing the correctly aligned memory address.
 * @return k_status_t Execution confirmation code.
 */
k_status_t heap_memalign(heap_context_t *heap, size_t alignment, size_t size, void **out_ptr);

/**
 * @brief Copies operational module usage metrics safely into diagnostic parameters.
 * @param heap Target handle identifying the active heap context instance.
 * @param used Output storage capturing currently consumed allocations payloads footprint.
 * @param total Output tracking variable capturing overall pool scale constraints.
 * @return k_status_t Execution confirmation code.
 */
k_status_t heap_get_stats(heap_context_t *heap, size_t *used, size_t *total);

/* --- Isolated Instance State Tracking Structural Node Context --- */
struct heap_context {
    paddr_t       vmm_root;     /* Associated VMM page table tree root anchor */
    vaddr_t       vaddr_base;   /* Base virtual address tracking location */
    size_t        total_size;   /* Aggregated pool footprint bytes */
    size_t        used_size;    /* Active registered payload metrics */
    heap_block_t *head;         /* Baseline initial header block index pointer */
};

#endif /* HEAP_H */
