#ifndef HEAP_H
#define HEAP_H

#include <type.h>

#define HEAP_MIN_BLOCK_SIZE  32
#define HEAP_MAGIC_ALLOCATED 0x414C4F43 /* "ALOC" */
#define HEAP_MAGIC_FREE      0x46524545 /* "FREE" */

/* --- Forward Declarations --- */
struct heap_context;

/* --- Boundary Tag Header Tracking Individual Block Metadata --- */
struct heap_block {
    u32           magic;         /* Verification tag checking corruption */
    bool               is_free;       /* Allocation tracking status flag */
    u64             size;          /* Total payload block size footprint in bytes */
    struct heap_block *next;          /* Single forward sibling block pointer */
    struct heap_block *prev;          /* Single reverse sibling block pointer */
};

/**
 * @brief Dynamically allocates virtual memory space via VMM and initializes a new heap instance.
 * @param root Target level 0 page descriptor mapping structure frame.
 * @param sz Initial continuous backing size requested for the heap instance.
 * @param out_heap Destination storage pointer capturing the generated heap context handle.
 * @return int Execution confirmation code.
 */
int heap_create(u64 root, u64 sz, struct heap_context **out_heap);

/**
 * @brief Completely dismantles a heap instance, releasing its virtual memory range back to the VMM.
 * @param heap Target handle identifying the active heap context instance to destroy.
 * @return int Execution confirmation code.
 */
int heap_destroy(struct heap_context *heap);

/**
 * @brief Allocates an arbitrary sequential chunk of byte-level payload space from a specific heap.
 * @param heap Target handle identifying the active heap context instance.
 * @param size Requested capacity footprint limits in bytes.
 * @param out_ptr Destination storage pointer capturing the generated memory allocation address.
 * @return int Execution confirmation code.
 */
int heap_malloc(struct heap_context *heap, u64 size, void **out_ptr);

/**
 * @brief Evaluates validation context maps and returns blocks back into active target pools.
 * @param heap Target handle identifying the active heap context instance.
 * @param ptr Target starting payload block location coordinates to free.
 * @return int Execution confirmation code.
 */
int heap_free(struct heap_context *heap, void *ptr);

/**
 * @brief Adjusts, migrates, or expands active sequential blocks safely within a specific heap.
 * @param heap Target handle identifying the active heap context instance.
 * @param ptr Target location anchoring current tracked operations blocks.
 * @param new_size Scale modification tracking limits.
 * @param out_ptr Destination storage pointer capturing the adjusted or migrated memory address.
 * @return int Execution confirmation code.
 */
int heap_realloc(struct heap_context *heap, void *ptr, u64 new_size, void **out_ptr);

/**
 * @brief Spawns custom byte alignments tracking specific architectural boundaries from a heap.
 * @param heap Target handle identifying the active heap context instance.
 * @param alignment Strict binary power-of-two mask constraint bounds.
 * @param size Requested storage sizing dimensions tracking allocations footprint.
 * @param out_ptr Destination storage pointer capturing the correctly aligned memory address.
 * @return int Execution confirmation code.
 */
int heap_memalign(struct heap_context *heap, u64 alignment, u64 size, void **out_ptr);

/**
 * @brief Copies operational module usage metrics safely into diagnostic parameters.
 * @param heap Target handle identifying the active heap context instance.
 * @param used Output storage capturing currently consumed allocations payloads footprint.
 * @param total Output tracking variable capturing overall pool scale constraints.
 * @return int Execution confirmation code.
 */
int heap_get_stats(struct heap_context *heap, u64 *used, u64 *total);

/* --- Isolated Instance State Tracking Structural Node Context --- */
struct heap_context {
    u64       vmm_root;     /* Associated VMM page table tree root anchor */
    u64       vaddr_base;   /* Base virtual address tracking location */
    u64        total_size;   /* Aggregated pool footprint bytes */
    u64        used_size;    /* Active registered payload metrics */
    struct heap_block *head;         /* Baseline initial header block index pointer */
};

#endif /* HEAP_H */
