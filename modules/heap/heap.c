#include "heap.h"
#include "../utils.h"
#include "../modules.h"
#include "../../include/api/vmm.h"
#include "../../include/errno.h"

EXTERN_IMPORT_INTERFACE(vmm, vmm);

#define MAX_HEAP_INSTANCES 16

/* --- Internal Core Heap State Engine Containers --- */
static struct heap_context s_heap_instances[MAX_HEAP_INSTANCES];

/* Alignment utility macros safeguarding native pointer width tracking configurations */
#define HEAP_ALIGN(x) (((x) + (sizeof(void *) - 1)) & ~(sizeof(void *) - 1))
#define BLOCK_HEADER_SIZE HEAP_ALIGN(sizeof(struct heap_block))
#define CONTEXT_SIZE HEAP_ALIGN(sizeof(struct heap_context))

/* Splits an active parent block down if remaining space satisfies minimum sizing bounds */
static void split_block(struct heap_block *block, u64 size)
{
	struct heap_block *new_block;
	u64 rem_size = block->size - size;

	if (rem_size < (BLOCK_HEADER_SIZE + HEAP_MIN_BLOCK_SIZE)) {
		return;
	}

	new_block = (struct heap_block *)((uintptr_t)block + BLOCK_HEADER_SIZE +
					  size);
	new_block->magic = HEAP_MAGIC_FREE;
	new_block->is_free = true;
	new_block->size = rem_size - BLOCK_HEADER_SIZE;
	new_block->next = block->next;
	new_block->prev = block;

	if (block->next) {
		block->next->prev = new_block;
	}
	block->next = new_block;
	block->size = size;
}

/* Merges contiguous free fragments forward and backward to prevent long-term layout fragmentation */
static void coalesce_blocks(struct heap_block *block)
{
	if (!block)
		return;

	/* Coalesce forward with consecutive sibling structures */
	if (block->next && block->next->is_free) {
		struct heap_block *next_block = block->next;
		block->size += BLOCK_HEADER_SIZE + next_block->size;
		block->next = next_block->next;
		if (next_block->next) {
			next_block->next->prev = block;
		}
		next_block->magic = 0;
	}

	/* Coalesce backward with previous sibling structures */
	if (block->prev && block->prev->is_free) {
		struct heap_block *prev_block = block->prev;
		prev_block->size += BLOCK_HEADER_SIZE + block->size;
		prev_block->next = block->next;
		if (block->next) {
			block->next->prev = prev_block;
		}
		block->magic = 0;
	}
}

/* Dynamically allocates virtual memory space via VMM and initializes a new heap instance */
int heap_create(u64 root, u64 sz, struct heap_context **out_heap)
{
	int status;
	u64 vaddr = 0;
	struct heap_context *heap_slot = NULL;
	struct heap_block *root_block;

	if (!root || sz <= (BLOCK_HEADER_SIZE + CONTEXT_SIZE) || !out_heap)
		return EINVAL;

	/* Find a vacant structural context tracking card frame slot */
	for (u64 i = 0; i < MAX_HEAP_INSTANCES; i++) {
		if (s_heap_instances[i].vaddr_base == 0) {
			heap_slot = &s_heap_instances[i];
			break;
		}
	}
	if (!heap_slot)
		return ENOMEM;

	/* Ask our underlying VMM to map a completely clear virtual block span context */
	status = vmm.allocate(root, &vaddr, sz, 0, VMM_REGION_HEAP);
	if (status)
		return status;

	sz = (sz + (4096 - 1)) & ~(4096 - 1);

	heap_slot->vmm_root = root;
	heap_slot->vaddr_base = vaddr;
	heap_slot->total_size = sz;
	heap_slot->used_size = 0;

	/* Carve out the baseline internal free chunk tracker across the remaining mapped block layout */
	root_block = (struct heap_block *)vaddr;
	root_block->magic = HEAP_MAGIC_FREE;
	root_block->is_free = true;
	root_block->size = sz - BLOCK_HEADER_SIZE;
	root_block->next = NULL;
	root_block->prev = NULL;

	heap_slot->head = root_block;
	*out_heap = heap_slot;

	return 0;
}

/* Completely dismantles a heap instance, releasing its virtual memory range back to the VMM */
int heap_destroy(struct heap_context *heap)
{
	int status;

	if (!heap || heap->vaddr_base == 0)
		return EINVAL;

	/* Clear virtual mapping directories and reclaim back physical context page tracking nodes */
	status = vmm.free(heap->vmm_root, heap->vaddr_base, heap->total_size);
	if (status)
		return status;

	kmemset(heap, 0, sizeof(struct heap_context));
	return 0;
}

/* Allocates an arbitrary sequential chunk of byte-level payload space from a specific heap */
int heap_malloc(struct heap_context *heap, u64 size, void **out_ptr)
{
	struct heap_block *curr;
	u64 aligned_size;

	if (!heap || !out_ptr)
		return EINVAL;
	if (size == 0) {
		*out_ptr = NULL;
		return 0;
	}

	aligned_size = HEAP_ALIGN(size);
	curr = heap->head;

	/* First-fit allocation strategy block search loop tied to this heap instance context */
	while (curr) {
		if (curr->is_free && curr->size >= aligned_size) {
			split_block(curr, aligned_size);
			curr->is_free = false;
			curr->magic = HEAP_MAGIC_ALLOCATED;
			heap->used_size += (BLOCK_HEADER_SIZE + curr->size);
			*out_ptr =
				(void *)((uintptr_t)curr + BLOCK_HEADER_SIZE);
			return 0;
		}
		curr = curr->next;
	}

	*out_ptr = NULL;
	return ENOMEM;
}

/* Evaluates validation context maps and returns blocks back into active target pools */
int heap_free(struct heap_context *heap, void *ptr)
{
	struct heap_block *block;

	if (!heap)
		return EINVAL;
	if (!ptr)
		return 0;

	block = (struct heap_block *)((uintptr_t)ptr - BLOCK_HEADER_SIZE);

	/* Anti-corruption security sanity checks */
	if (block->magic != HEAP_MAGIC_ALLOCATED)
		return EINVAL;

	block->is_free = true;
	block->magic = HEAP_MAGIC_FREE;

	if (heap->used_size >= (BLOCK_HEADER_SIZE + block->size)) {
		heap->used_size -= (BLOCK_HEADER_SIZE + block->size);
	} else {
		heap->used_size = 0;
	}

	coalesce_blocks(block);
	return 0;
}

/* Adjusts, migrates, or expands active sequential blocks safely within a specific heap */
int heap_realloc(struct heap_context *heap, void *ptr, u64 new_size,
		 void **out_ptr)
{
	struct heap_block *block;
	u64 aligned_size;
	int status;
	void *new_ptr;

	if (!heap || !out_ptr)
		return EINVAL;
	if (!ptr)
		return heap_malloc(heap, new_size, out_ptr);
	if (new_size == 0) {
		heap_free(heap, ptr);
		*out_ptr = NULL;
		return 0;
	}

	block = (struct heap_block *)((uintptr_t)ptr - BLOCK_HEADER_SIZE);
	aligned_size = HEAP_ALIGN(new_size);

	if (block->magic != HEAP_MAGIC_ALLOCATED)
		return EINVAL;

	/* If current block satisfies boundaries already, check if we can reuse or downsize it */
	if (block->size >= aligned_size) {
		split_block(block, aligned_size);
		*out_ptr = ptr;
		return 0;
	}

	/* Optimization Pass: Check if consecutive neighbor is free and can accommodate expanding size */
	if (block->next && block->next->is_free &&
	    (block->size + BLOCK_HEADER_SIZE + block->next->size) >=
		    aligned_size) {
		struct heap_block *next_block = block->next;
		heap->used_size -= (BLOCK_HEADER_SIZE + block->size);

		block->size += BLOCK_HEADER_SIZE + next_block->size;
		block->next = next_block->next;
		if (next_block->next) {
			next_block->next->prev = block;
		}
		next_block->magic = 0;

		split_block(block, aligned_size);
		heap->used_size += (BLOCK_HEADER_SIZE + block->size);
		*out_ptr = ptr;
		return 0;
	}

	/* Fallback Migration Case: Allocate a separate destination range and mirror existing data payload */
	status = heap_malloc(heap, new_size, &new_ptr);
	if (status)
		return status;

	kmemcpy(new_ptr, ptr, block->size);
	heap_free(heap, ptr);

	*out_ptr = new_ptr;
	return 0;
}

/* Spawns custom byte alignments tracking specific architectural boundaries from a heap */
int heap_memalign(struct heap_context *heap, u64 alignment, u64 size,
		  void **out_ptr)
{
	u64 raw_size;
	void *raw_ptr;
	uintptr_t raw_addr;
	uintptr_t aligned_addr;
	u64 adjustment;
	int status;
	struct heap_block *raw_block;
	struct heap_block *aligned_block;

	if (!heap || !out_ptr)
		return EINVAL;

	/* Enforce binary power-of-two constraints configuration limits */
	if ((alignment & (alignment - 1)) != 0 || alignment == 0)
		return EINVAL;
	if (alignment < sizeof(void *))
		return heap_malloc(heap, size, out_ptr);

	/* Allocate excess pad overhead space to shift within requested alignment boundaries */
	raw_size = size + alignment + BLOCK_HEADER_SIZE;
	status = heap_malloc(heap, raw_size, &raw_ptr);
	if (status)
		return status;

	raw_addr = (uintptr_t)raw_ptr;
	aligned_addr = (raw_addr + BLOCK_HEADER_SIZE + alignment - 1) &
		       ~(alignment - 1);
	adjustment = aligned_addr - raw_addr;

	if (adjustment == 0) {
		*out_ptr = raw_ptr;
		return 0;
	}

	/* Trace original tracked structure layout offsets and split alignment headers */
	raw_block = (struct heap_block *)(raw_addr - BLOCK_HEADER_SIZE);
	heap->used_size -= (BLOCK_HEADER_SIZE + raw_block->size);

	aligned_block = (struct heap_block *)(aligned_addr - BLOCK_HEADER_SIZE);
	aligned_block->magic = HEAP_MAGIC_ALLOCATED;
	aligned_block->is_free = false;
	aligned_block->size = raw_block->size - adjustment;
	aligned_block->next = raw_block->next;
	aligned_block->prev = raw_block;

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
	return 0;
}

/* Copies operational module usage metrics safely into diagnostic parameters */
int heap_get_stats(struct heap_context *heap, u64 *used, u64 *total)
{
	if (!heap)
		return EINVAL;
	if (used)
		*used = heap->used_size;
	if (total)
		*total = heap->total_size;
	return 0;
}
