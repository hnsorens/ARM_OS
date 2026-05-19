#include "heap.h"
#include "../utils.h"
#include "../modules.h"
#include "../../include/api/vmm.h"

EXTERN_IMPORT_INTERFACE(vmm, vmm);

#define MAX_HEAP_INSTANCES 16

/* --- Internal Core Heap State Engine Containers --- */
static heap_context_t s_heap_instances[MAX_HEAP_INSTANCES];

/* Alignment utility macros safeguarding native pointer width tracking configurations */
#define HEAP_ALIGN(x)      (((x) + (sizeof(void*) - 1)) & ~(sizeof(void*) - 1))
#define BLOCK_HEADER_SIZE  HEAP_ALIGN(sizeof(heap_block_t))
#define CONTEXT_SIZE       HEAP_ALIGN(sizeof(heap_context_t))

/* Splits an active parent block down if remaining space satisfies minimum sizing bounds */
static void split_block(heap_block_t *block, size_t size) {
    heap_block_t *new_block;
    size_t        rem_size = block->size - size;

    if (rem_size < (BLOCK_HEADER_SIZE + HEAP_MIN_BLOCK_SIZE)) {
        return; 
    }

    new_block = (heap_block_t *)((uintptr_t)block + BLOCK_HEADER_SIZE + size);
    new_block->magic   = HEAP_MAGIC_FREE;
    new_block->is_free = true;
    new_block->size    = rem_size - BLOCK_HEADER_SIZE;
    new_block->next    = block->next;
    new_block->prev    = block;

    if (block->next) {
        block->next->prev = new_block;
    }
    block->next = new_block;
    block->size = size;
}

/* Merges contiguous free fragments forward and backward to prevent long-term layout fragmentation */
static void coalesce_blocks(heap_block_t *block) {
    if (!block) return;

    /* Coalesce forward with consecutive sibling structures */
    if (block->next && block->next->is_free) {
        heap_block_t *next_block = block->next;
        block->size += BLOCK_HEADER_SIZE + next_block->size;
        block->next  = next_block->next;
        if (next_block->next) {
            next_block->next->prev = block;
        }
        next_block->magic = 0; 
    }

    /* Coalesce backward with previous sibling structures */
    if (block->prev && block->prev->is_free) {
        heap_block_t *prev_block = block->prev;
        prev_block->size += BLOCK_HEADER_SIZE + block->size;
        prev_block->next  = block->next;
        if (block->next) {
            block->next->prev = prev_block;
        }
        block->magic = 0;
    }
}

/* Dynamically allocates virtual memory space via VMM and initializes a new heap instance */
k_status_t heap_create(paddr_t root, size_t sz, heap_context_t **out_heap) {
    k_status_t      status;
    vaddr_t         vaddr = 0;
    heap_context_t *heap_slot = NULL;
    heap_block_t   *root_block;

    if (!root || sz <= (BLOCK_HEADER_SIZE + CONTEXT_SIZE) || !out_heap) return K_STATUS_INVALID_ARG;

    /* Find a vacant structural context tracking card frame slot */
    for (size_t i = 0; i < MAX_HEAP_INSTANCES; i++) {
        if (s_heap_instances[i].vaddr_base == 0) {
            heap_slot = &s_heap_instances[i];
            break;
        }
    }
    if (!heap_slot) return K_STATUS_OUT_OF_MEMORY;

    /* Ask our underlying VMM to map a completely clear virtual block span context */
    status = vmm.allocate(root, &vaddr, sz, MMU_READ | MMU_WRITE, VMM_REGION_HEAP);
    if (k_error(status)) return status;

    sz = (sz + (4096 - 1)) & ~(4096 - 1);

    heap_slot->vmm_root   = root;
    heap_slot->vaddr_base = vaddr;
    heap_slot->total_size = sz;
    heap_slot->used_size  = 0;

    /* Carve out the baseline internal free chunk tracker across the remaining mapped block layout */
    root_block = (heap_block_t *)vaddr;
    root_block->magic   = HEAP_MAGIC_FREE;
    root_block->is_free = true;
    root_block->size    = sz - BLOCK_HEADER_SIZE;
    root_block->next    = NULL;
    root_block->prev    = NULL;

    heap_slot->head = root_block;
    *out_heap       = heap_slot;

    return K_STATUS_OK;
}

/* Completely dismantles a heap instance, releasing its virtual memory range back to the VMM */
k_status_t heap_destroy(heap_context_t *heap) {
    k_status_t status;

    if (!heap || heap->vaddr_base == 0) return K_STATUS_INVALID_ARG;

    /* Clear virtual mapping directories and reclaim back physical context page tracking nodes */
    status = vmm.free(heap->vmm_root, heap->vaddr_base, heap->total_size);
    if (k_error(status)) return status;

    memset(heap, 0, sizeof(heap_context_t));
    return K_STATUS_OK;
}

/* Allocates an arbitrary sequential chunk of byte-level payload space from a specific heap */
k_status_t heap_malloc(heap_context_t *heap, size_t size, void **out_ptr) {
    heap_block_t *curr;
    size_t        aligned_size;

    if (!heap || !out_ptr) return K_STATUS_INVALID_ARG;
    if (size == 0) {
        *out_ptr = NULL;
        return K_STATUS_OK;
    }

    aligned_size = HEAP_ALIGN(size);
    curr         = heap->head;

    /* First-fit allocation strategy block search loop tied to this heap instance context */
    while (curr) {
        if (curr->is_free && curr->size >= aligned_size) {
            split_block(curr, aligned_size);
            curr->is_free  = false;
            curr->magic    = HEAP_MAGIC_ALLOCATED;
            heap->used_size += (BLOCK_HEADER_SIZE + curr->size);
            *out_ptr = (void *)((uintptr_t)curr + BLOCK_HEADER_SIZE);
            return K_STATUS_OK;
        }
        curr = curr->next;
    }

    *out_ptr = NULL;
    return K_STATUS_OUT_OF_MEMORY; 
}

/* Evaluates validation context maps and returns blocks back into active target pools */
k_status_t heap_free(heap_context_t *heap, void *ptr) {
    heap_block_t *block;

    if (!heap) return K_STATUS_INVALID_ARG;
    if (!ptr) return K_STATUS_OK;

    block = (heap_block_t *)((uintptr_t)ptr - BLOCK_HEADER_SIZE);

    /* Anti-corruption security sanity checks */
    if (block->magic != HEAP_MAGIC_ALLOCATED) return K_STATUS_BAD_STATE;

    block->is_free = true;
    block->magic   = HEAP_MAGIC_FREE;
    
    if (heap->used_size >= (BLOCK_HEADER_SIZE + block->size)) {
        heap->used_size -= (BLOCK_HEADER_SIZE + block->size);
    } else {
        heap->used_size = 0;
    }

    coalesce_blocks(block);
    return K_STATUS_OK;
}

/* Adjusts, migrates, or expands active sequential blocks safely within a specific heap */
k_status_t heap_realloc(heap_context_t *heap, void *ptr, size_t new_size, void **out_ptr) {
    heap_block_t *block;
    size_t        aligned_size;
    k_status_t    status;
    void         *new_ptr;

    if (!heap || !out_ptr) return K_STATUS_INVALID_ARG;
    if (!ptr) return heap_malloc(heap, new_size, out_ptr);
    if (new_size == 0) {
        heap_free(heap, ptr);
        *out_ptr = NULL;
        return K_STATUS_OK;
    }

    block        = (heap_block_t *)((uintptr_t)ptr - BLOCK_HEADER_SIZE);
    aligned_size = HEAP_ALIGN(new_size);

    if (block->magic != HEAP_MAGIC_ALLOCATED) return K_STATUS_BAD_STATE;

    /* If current block satisfies boundaries already, check if we can reuse or downsize it */
    if (block->size >= aligned_size) {
        split_block(block, aligned_size);
        *out_ptr = ptr;
        return K_STATUS_OK;
    }

    /* Optimization Pass: Check if consecutive neighbor is free and can accommodate expanding size */
    if (block->next && block->next->is_free && 
        (block->size + BLOCK_HEADER_SIZE + block->next->size) >= aligned_size) {
        
        heap_block_t *next_block = block->next;
        heap->used_size -= (BLOCK_HEADER_SIZE + block->size);

        block->size += BLOCK_HEADER_SIZE + next_block->size;
        block->next  = next_block->next;
        if (next_block->next) {
            next_block->next->prev = block;
        }
        next_block->magic = 0;

        split_block(block, aligned_size);
        heap->used_size += (BLOCK_HEADER_SIZE + block->size);
        *out_ptr = ptr;
        return K_STATUS_OK;
    }

    /* Fallback Migration Case: Allocate a separate destination range and mirror existing data payload */
    status = heap_malloc(heap, new_size, &new_ptr);
    if (k_error(status)) return status;

    memcpy(new_ptr, ptr, block->size);
    heap_free(heap, ptr);

    *out_ptr = new_ptr;
    return K_STATUS_OK;
}

/* Spawns custom byte alignments tracking specific architectural boundaries from a heap */
k_status_t heap_memalign(heap_context_t *heap, size_t alignment, size_t size, void **out_ptr) {
    size_t        raw_size;
    void         *raw_ptr;
    uintptr_t     raw_addr;
    uintptr_t     aligned_addr;
    size_t        adjustment;
    k_status_t    status;
    heap_block_t *raw_block;
    heap_block_t *aligned_block;

    if (!heap || !out_ptr) return K_STATUS_INVALID_ARG;

    /* Enforce binary power-of-two constraints configuration limits */
    if ((alignment & (alignment - 1)) != 0 || alignment == 0) return K_STATUS_INVALID_ARG;
    if (alignment < sizeof(void*)) return heap_malloc(heap, size, out_ptr);

    /* Allocate excess pad overhead space to shift within requested alignment boundaries */
    raw_size = size + alignment + BLOCK_HEADER_SIZE;
    status   = heap_malloc(heap, raw_size, &raw_ptr);
    if (k_error(status)) return status;

    raw_addr     = (uintptr_t)raw_ptr;
    aligned_addr = (raw_addr + BLOCK_HEADER_SIZE + alignment - 1) & ~(alignment - 1);
    adjustment   = aligned_addr - raw_addr;

    if (adjustment == 0) {
        *out_ptr = raw_ptr;
        return K_STATUS_OK; 
    }

    /* Trace original tracked structure layout offsets and split alignment headers */
    raw_block = (heap_block_t *)(raw_addr - BLOCK_HEADER_SIZE);
    heap->used_size -= (BLOCK_HEADER_SIZE + raw_block->size);

    aligned_block = (heap_block_t *)(aligned_addr - BLOCK_HEADER_SIZE);
    aligned_block->magic   = HEAP_MAGIC_ALLOCATED;
    aligned_block->is_free = false;
    aligned_block->size    = raw_block->size - adjustment;
    aligned_block->next    = raw_block->next;
    aligned_block->prev    = raw_block;

    if (raw_block->next) {
        raw_block->next->prev = aligned_block;
    }
    raw_block->next = aligned_block;
    raw_block->size = adjustment - BLOCK_HEADER_SIZE;
    raw_block->magic = HEAP_MAGIC_ALLOCATED;

    heap->used_size += (BLOCK_HEADER_SIZE + raw_block->size);
    heap->used_size += (BLOCK_HEADER_SIZE + aligned_block->size);

    /* Release preceding padding blocks back into the pool to preserve density metrics */
    heap_free(heap, (void *)raw_ptr);

    *out_ptr = (void *)aligned_addr;
    return K_STATUS_OK;
}

/* Copies operational module usage metrics safely into diagnostic parameters */
k_status_t heap_get_stats(heap_context_t *heap, size_t *used, size_t *total) {
    if (!heap) return K_STATUS_INVALID_ARG;
    if (used)  *used  = heap->used_size;
    if (total) *total = heap->total_size;
    return K_STATUS_OK;
}

