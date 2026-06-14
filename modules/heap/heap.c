#include "heap.h"
#include "../utils.h"
#include "../modules.h"
#include "../../include/api/vmm.h"
#include "../../include/api/serial_debug.h"
#include "../../include/api/serial_debug.h"
#include "../../include/errno.h"

EXTERN_IMPORT_INTERFACE(vmm, vmm);
EXTERN_IMPORT_INTERFACE(serial, serial);

/* Enforce strict 16-byte architecture alignment safety loops */
#define HEAP_ALIGN(x) (((x) + 15) & ~15)
#define BLOCK_HEADER_SIZE HEAP_ALIGN(sizeof(struct heap_block))
#define CONTEXT_SIZE HEAP_ALIGN(sizeof(struct heap_context))
#define CONTEXT_SIZE HEAP_ALIGN(sizeof(struct heap_context))

/* Splits an active parent block down if remaining space satisfies minimum sizing bounds */
static void split_block(struct heap_block *block, u64 size)
{
	struct heap_block *new_block;
	u64 rem_size = block->size - size;

	// If the remaining space is too small to form a new block, return
	if (rem_size < (BLOCK_HEADER_SIZE + HEAP_MIN_BLOCK_SIZE)) {
		return;
	}

	/* Enforce explicit byte positioning for the split block */
	uintptr_t new_block_addr = (uintptr_t)block + BLOCK_HEADER_SIZE + size;
	new_block = (struct heap_block *)new_block_addr;

	new_block->magic = HEAP_MAGIC_FREE;
	new_block->is_free = true;
	new_block->size = rem_size - BLOCK_HEADER_SIZE;
	new_block->next = block->next;
	new_block->prev = block;

	// Update the next block's previous pointer if it exists
	if (block->next) {
		block->next->prev = new_block;
	}
	block->next = new_block;
	block->size = size;
}

/* Merges contiguous free fragments forward and backward to prevent long-term layout fragmentation */
// Merges contiguous free fragments forward and backward to prevent long-term layout fragmentation
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
// Dynamically allocates virtual memory space via VMM and initializes a new heap instance
int heap_create(u64 root, u64 sz, struct heap_context **out_heap)
{
	int status;
	u64 vaddr = 0xFFFF800400000000ULL;
	struct heap_context *heap_slot;
	struct heap_block *root_block;

	if (!root || !out_heap)
		return EINVAL;

	// Ensure the requested size can easily hold the context header, the first block header, and baseline data
	// Add some padding to ensure alignment and space for future allocations
	sz += CONTEXT_SIZE + BLOCK_HEADER_SIZE;
	sz = (sz + (4096 - 1)) & ~(4096 - 1);

	/* Ask underlying VMM to allocate the raw page block */
	status = vmm.allocate(root, &vaddr, sz, 0x3, VMM_REGION_HEAP);
	if (status)
		return status;

	/* Calculate addresses explicitly using raw integer bytes to prevent scaling issues */
	// Calculate addresses explicitly using raw integer bytes to prevent scaling issues
	// Ensure the base address and root block address are correctly aligned
	uintptr_t base_address = (uintptr_t)vaddr;
	uintptr_t root_block_address = base_address + CONTEXT_SIZE;

	/* Self-Bootstrap: Place the context struct cleanly at the virtual address base */

	heap_slot = (struct heap_context *)base_address;
	heap_slot->vmm_root = root;
	heap_slot->vaddr_base = vaddr;
	heap_slot->total_size = sz;
	heap_slot->used_size = 0;

	/* Place the initial root block strictly past the context layout structure boundary */
	root_block = (struct heap_block *)root_block_address;
	root_block->magic = HEAP_MAGIC_FREE;
	root_block->is_free = true;
	root_block->size = sz - CONTEXT_SIZE - BLOCK_HEADER_SIZE;
	root_block->next = NULL;
	root_block->prev = NULL;

	heap_slot->head = root_block;
	*out_heap = heap_slot;

	return 0;
}

// Completely dismantles a heap instance, releasing its virtual memory range back to the VMM
/* Completely dismantles a heap instance, releasing its virtual memory range back to the VMM */
/**
 * Completely dismantles a heap instance, releasing its virtual memory range back to the VMM.
 *
 * @param heap Pointer to the heap context
 * @return Status code indicating success or failure
 */
int heap_destroy(struct heap_context *heap)
{
	if (!heap || heap->vaddr_base == 0)
		return EINVAL;

	return vmm.free(heap->vmm_root, heap->vaddr_base, heap->total_size);
}

/* Allocates an arbitrary sequential chunk of byte-level payload space from a specific heap */
// Uses a first-fit allocation strategy to find and allocate memory
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

	/* First-fit allocation strategy block search loop */
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
// Frees an allocated block and attempts to coalesce with adjacent free blocks
int heap_free(struct heap_context *heap, void *ptr)
{
	struct heap_block *block;

	if (!heap || !ptr)
		return (!heap) ? EINVAL : 0;

	block = (struct heap_block *)((uintptr_t)ptr - BLOCK_HEADER_SIZE);

	/* Anti-corruption security sanity checks */
	if (block->magic != HEAP_MAGIC_ALLOCATED) {
		return EINVAL;
	}

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
// Reallocates memory by either resizing the existing block or allocating a new one
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

	if (block->magic != HEAP_MAGIC_ALLOCATED) {
		return EINVAL;
	}

	/* If current block satisfies boundaries already, reuse or downsize it */
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
// Allocates memory with a specified alignment
int heap_memalign(struct heap_context *heap, u64 alignment, u64 size,
		  void **out_ptr)
{
	struct heap_block *curr;
	u64 aligned_size;

	if (!heap || !out_ptr)
		return EINVAL;

	/* Enforce strict power-of-two mask validations */
	if (alignment == 0 || (alignment & (alignment - 1)) != 0)
		return EINVAL;

	if (alignment < 16)
		alignment = 16;

	if (size == 0) {
		*out_ptr = NULL;
		return 0;
	}

	aligned_size = HEAP_ALIGN(size);
	// Iterate through the list of blocks to find a suitable free block
	curr = heap->head;

	while (curr) {
		if (curr->is_free) {
			uintptr_t raw_payload_addr =
				(uintptr_t)curr + BLOCK_HEADER_SIZE;
			uintptr_t aligned_payload_addr =
				(raw_payload_addr + alignment - 1) &
				~(alignment - 1);
			u64 padding = aligned_payload_addr - raw_payload_addr;

			/* Scenario A: The current free block raw pointer perfectly aligns natively */
			if (padding == 0 && curr->size >= aligned_size) {
				split_block(curr, aligned_size);
				curr->is_free = false;
				curr->magic = HEAP_MAGIC_ALLOCATED;
				heap->used_size +=
					(BLOCK_HEADER_SIZE + curr->size);
				*out_ptr = (void *)aligned_payload_addr;
				return 0;
			}

			/* NEW FIX: If padding is too small to form a split header, push it forward 
               by alignment increments until it is large enough. */
			u64 min_required_padding =
				BLOCK_HEADER_SIZE + HEAP_MIN_BLOCK_SIZE;
			if (padding > 0 && padding < min_required_padding) {
				u64 remaining_pad =
					min_required_padding - padding;
				// Round up the remaining padding needed to the next multiple of alignment
				u64 alignment_chunks =
					(remaining_pad + alignment - 1) &
					~(alignment - 1);
				padding += alignment_chunks;
				aligned_payload_addr =
					raw_payload_addr + padding;
			}

			/* Scenario B: Offset padding needed (guaranteed now to be safely splittable) */
			if (padding >= min_required_padding &&
			    curr->size >= (padding + aligned_size)) {
				u64 old_total_size = curr->size;

				/* Shrink current block to act as the front pad block */
				curr->size = padding - BLOCK_HEADER_SIZE;

				struct heap_block *aligned_block =
					(struct heap_block
						 *)(aligned_payload_addr -
						    BLOCK_HEADER_SIZE);
				aligned_block->magic = HEAP_MAGIC_FREE;
				aligned_block->is_free = true;
				aligned_block->size = old_total_size - padding;

				/* Link new aligned block into tracking context BEFORE splitting to keep links linear */
				aligned_block->next = curr->next;
				aligned_block->prev = curr;
				if (curr->next) {
					curr->next->prev = aligned_block;
				}
				curr->next = aligned_block;

				/* Now safe to split trailing excess off the back end of the aligned block */
				split_block(aligned_block, aligned_size);
				aligned_block->is_free = false;
				aligned_block->magic = HEAP_MAGIC_ALLOCATED;

				/* Correctly track exact dynamic footprints */
				heap->used_size += (BLOCK_HEADER_SIZE +
						    aligned_block->size);
				*out_ptr = (void *)aligned_payload_addr;
				return 0;
			}
		}
		curr = curr->next;
	}

	*out_ptr = NULL;
	return ENOMEM;
}

/* Copies operational module usage metrics safely into diagnostic parameters */
/**
 * Copies operational module usage metrics safely into diagnostic parameters.
 *
 * @param heap Pointer to the heap context
 * @param used Pointer to store the used memory size
 * @param total Pointer to store the total memory size
 * @return Status code indicating success or failure
 */
// Retrieves the used and total memory sizes from a heap
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
