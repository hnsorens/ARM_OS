/**
 * @file heap.c
 * @brief Core Implementation details of the Intrusive Boundary-Tag Allocator Engine.
 */

#include "heap.h"
#include <utils.h>
#include <modules.h>
#include <api/vmm.h>
#include <api/serial_debug.h>
#include <errno.h>

/* --- Core Module Linkage Hooks --- */
EXTERN_IMPORT_INTERFACE(vmm, vmm);
EXTERN_IMPORT_INTERFACE(serial, serial);

/* --- Alignment Scaling Macrology --- */
#define HEAP_ALIGN(x) \
	(((x) + 15) & \
	 ~15) /**< Quantize sizing metrics up to 16-byte boundaries */
#define BLOCK_HEADER_SIZE  \
	HEAP_ALIGN(sizeof( \
		struct heap_block)) /**< Granular size footprint of individual entry headers */
#define CONTEXT_SIZE       \
	HEAP_ALIGN(sizeof( \
		struct heap_context)) /**< Structural layout alignment boundary for base tracking headers */

/**
 * @brief Internal Helper: Carves a single parent block layout matrix into two discrete entries.
 * * Splitting isolates the required tracking size footprint, making the remaining address remainder 
 * available as a new standalone item in the free list.
 * * @param[in,out] block Target parent block tracking structure context.
 * @param[in]     size  Desired internal payload storage dimension to allocate.
 */
static void split_block(struct heap_block *block, u64 size)
{
	struct heap_block *new_block;
	u64 rem_size = block->size - size;

	/* Abort split if remainder space fails minimum formatting thresholds */
	if (rem_size < (BLOCK_HEADER_SIZE + HEAP_MIN_BLOCK_SIZE)) {
		return;
	}

	/* Derive precise layout offset target address for the trailing block */
	uintptr_t new_block_addr = (uintptr_t)block + BLOCK_HEADER_SIZE + size;
	new_block = (struct heap_block *)new_block_addr;

	/* Initialize structural fields on the newly generated free remainder block */
	new_block->magic = HEAP_MAGIC_FREE;
	new_block->is_free = true;
	new_block->size = rem_size - BLOCK_HEADER_SIZE;
	new_block->next = block->next;
	new_block->prev = block;

	/* Intrusively stitch the remainder block into the tracking list sequence */
	if (block->next) {
		block->next->prev = new_block;
	}
	block->next = new_block;
	block->size = size;
}

/**
 * @brief Internal Helper: Coalesces contiguous unallocated blocks to prevent layout fragmentation.
 * * Inspects both subsequent (+1) and antecedent (-1) linear memory address spaces, merging 
 * adjacent free blocks into a single continuous tracking header object.
 * * @param[in,out] block Anchor entry node from which coalescing metrics sweep out.
 */
static void coalesce_blocks(struct heap_block *block)
{
	if (!block)
		return;

	/* Forward Sweep Phase: Merge matching continuous properties into current context block */
	if (block->next && block->next->is_free) {
		struct heap_block *next_block = block->next;
		block->size += BLOCK_HEADER_SIZE + next_block->size;
		block->next = next_block->next;
		if (next_block->next) {
			next_block->next->prev = block;
		}
		next_block->magic =
			0; /* Clear signature field context on stale entry */
	}

	/* Backward Sweep Phase: Merge current context properties into ancestral structural entries */
	if (block->prev && block->prev->is_free) {
		struct heap_block *prev_block = block->prev;
		prev_block->size += BLOCK_HEADER_SIZE + block->size;
		prev_block->next = block->next;
		if (block->next) {
			block->next->prev = prev_block;
		}
		block->magic =
			0; /* Clear signature field context on stale entry */
	}
}

int heap_create(u64 root, u64 sz, struct heap_context **out_heap)
{
	int status = 0;
	u64 vaddr =
		0xFFFF900000000000ULL; /* Standardized Kernel canonical heap tracking base region address space */
	struct heap_context *heap_slot = NULL;
	struct heap_block *root_block = NULL;
	uintptr_t base_address;
	uintptr_t root_block_address;

	if (!root || !out_heap) {
		status = EINVAL;
		goto cleanup;
	}

	/* Factoring operational header spaces into total size parameters */
	sz += CONTEXT_SIZE + BLOCK_HEADER_SIZE;
	sz = (sz + (4096 - 1)) &
	     ~(4096 -
	       1); /* Page align total allocation footprint dimensions up to 4KB intervals */

	/* Commit underlying page frameworks using kernel standard flags (Present, Write, Cache-Disable, Global) */
	status = vmm.allocate(root, &vaddr, sz, 0x713, VMM_REGION_HEAP);
	if (status) {
		goto cleanup;
	}

	/* Establish structural pointer metrics relative to the newly pinned memory layout block */
	base_address = (uintptr_t)vaddr;
	root_block_address = base_address + CONTEXT_SIZE;

	/* Self-Bootstrap Pass: Construct the primary master driver block at address zero offset */
	heap_slot = (struct heap_context *)base_address;
	heap_slot->vmm_root = root;
	heap_slot->vaddr_base = vaddr;
	heap_slot->total_size = sz;
	heap_slot->used_size = 0;

	/* Initialize the baseline monolithic root free block spanning the remaining region capacity */
	root_block = (struct heap_block *)root_block_address;
	root_block->magic = HEAP_MAGIC_FREE;
	root_block->is_free = true;
	root_block->size = sz - CONTEXT_SIZE - BLOCK_HEADER_SIZE;
	root_block->next = NULL;
	root_block->prev = NULL;

	heap_slot->head = root_block;
	*out_heap = heap_slot;

cleanup:
	return status;
}

int heap_destroy(struct heap_context *heap)
{
	int status = 0;

	if (!heap || heap->vaddr_base == 0) {
		status = EINVAL;
		goto cleanup;
	}

	/* Drop entire backing virtual address window sequence maps directly out of the VMM structures */
	status = vmm.free(heap->vmm_root, heap->vaddr_base, heap->total_size);

cleanup:
	return status;
}

int heap_malloc(struct heap_context *heap, u64 size, void **out_ptr)
{
	int status = 0;
	struct heap_block *curr = NULL;
	u64 aligned_size;

	if (!heap || !out_ptr) {
		status = EINVAL;
		goto cleanup;
	}

	if (size == 0) {
		*out_ptr = NULL;
		goto cleanup;
	}

	aligned_size = HEAP_ALIGN(size);
	curr = heap->head;

	/* Parse intrusive node chain under classical First-Fit heuristics criteria */
	while (curr) {
		if (curr->is_free && curr->size >= aligned_size) {
			/* Carve away excess space capacity properties into discrete unallocated slots */
			split_block(curr, aligned_size);

			/* Update boundary descriptors and metrics to mark the block as allocated */
			curr->is_free = false;
			curr->magic = HEAP_MAGIC_ALLOCATED;
			heap->used_size += (BLOCK_HEADER_SIZE + curr->size);

			/* Compute target payload address offset exactly past metadata fields boundary */
			*out_ptr =
				(void *)((uintptr_t)curr + BLOCK_HEADER_SIZE);
			goto cleanup;
		}
		curr = curr->next;
	}

	/* First-fit fallthrough represents exhausting available free blocks */
	*out_ptr = NULL;
	status = ENOMEM;

cleanup:
	return status;
}

int heap_free(struct heap_context *heap, void *ptr)
{
	int status = 0;
	struct heap_block *block = NULL;

	if (!heap || !ptr) {
		status =
			(!heap) ?
				EINVAL :
				0; /* Standard POSIX-compliant no-op behavior pattern on free(NULL) */
		goto cleanup;
	}

	/* Extrapolate metadata memory position using backward address stride matching block size headers */
	block = (struct heap_block *)((uintptr_t)ptr - BLOCK_HEADER_SIZE);

	/* Anti-Corruption Sanity Pass: Trap invalid pointers or memory corruptions via validation field check */
	if (block->magic != HEAP_MAGIC_ALLOCATED) {
		status = EINVAL;
		goto cleanup;
	}

	/* Flip tracking states back into structural pools safely */
	block->is_free = true;
	block->magic = HEAP_MAGIC_FREE;

	/* Recalculate operational tracker size metrics carefully to prevent underflow wrap errors */
	if (heap->used_size >= (BLOCK_HEADER_SIZE + block->size)) {
		heap->used_size -= (BLOCK_HEADER_SIZE + block->size);
	} else {
		heap->used_size = 0;
	}

	/* Clean tracking layout structures immediately to preserve continuous memory spans */
	coalesce_blocks(block);

cleanup:
	return status;
}

int heap_realloc(struct heap_context *heap, void *ptr, u64 new_size,
		 void **out_ptr)
{
	int status = 0;
	struct heap_block *block = NULL;
	struct heap_block *next_block = NULL;
	u64 aligned_size;
	void *new_ptr = NULL;

	if (!heap || !out_ptr) {
		status = EINVAL;
		goto cleanup;
	}

	if (!ptr) {
		status = heap_malloc(heap, new_size, out_ptr);
		goto cleanup;
	}

	if (new_size == 0) {
		heap_free(heap, ptr);
		*out_ptr = NULL;
		goto cleanup;
	}

	block = (struct heap_block *)((uintptr_t)ptr - BLOCK_HEADER_SIZE);
	aligned_size = HEAP_ALIGN(new_size);

	if (block->magic != HEAP_MAGIC_ALLOCATED) {
		status = EINVAL;
		goto cleanup;
	}

	/* Path Alpha: Block capacity already accommodates requirements. Shrink/Reuse in-place. */
	if (block->size >= aligned_size) {
		split_block(block, aligned_size);
		*out_ptr = ptr;
		goto cleanup;
	}

	/* Path Beta (Optimization): Assess contiguous forward neighbor capacity to avoid data migration */
	if (block->next && block->next->is_free &&
	    (block->size + BLOCK_HEADER_SIZE + block->next->size) >=
		    aligned_size) {
		next_block = block->next;
		heap->used_size -= (BLOCK_HEADER_SIZE + block->size);

		/* Consume the adjacent free block's memory space */
		block->size += BLOCK_HEADER_SIZE + next_block->size;
		block->next = next_block->next;
		if (next_block->next) {
			next_block->next->prev = block;
		}
		next_block->magic =
			0; /* Invalidate consumed descriptor token metadata */

		/* Split remaining unallocated trailing margins out cleanly */
		split_block(block, aligned_size);
		heap->used_size += (BLOCK_HEADER_SIZE + block->size);
		*out_ptr = ptr;
		goto cleanup;
	}

	/* Path Gamma (Fallback Migration): Allocate a new memory span, clone payload contents, and free original block */
	status = heap_malloc(heap, new_size, &new_ptr);
	if (status) {
		goto cleanup;
	}

	kmemcpy(new_ptr, ptr, block->size);
	heap_free(heap, ptr);
	*out_ptr = new_ptr;

cleanup:
	return status;
}

int heap_memalign(struct heap_context *heap, u64 alignment, u64 size,
		  void **out_ptr)
{
	int status = 0;
	struct heap_block *curr = NULL;
	struct heap_block *aligned_block = NULL;
	u64 aligned_size;
	u64 min_required_padding;

	if (!heap || !out_ptr) {
		status = EINVAL;
		goto cleanup;
	}

	/* Architectural Guard: Verify alignment properties conform to strict power-of-two constraints */
	if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
		status = EINVAL;
		goto cleanup;
	}

	/* Enforce system 16-byte minimum alignment constraints universally */
	if (alignment < 16)
		alignment = 16;

	if (size == 0) {
		*out_ptr = NULL;
		goto cleanup;
	}

	aligned_size = HEAP_ALIGN(size);
	curr = heap->head;

	while (curr) {
		if (curr->is_free) {
			/* Compute structural payload target positions and delta offsets */
			uintptr_t raw_payload_addr =
				(uintptr_t)curr + BLOCK_HEADER_SIZE;
			uintptr_t aligned_payload_addr =
				(raw_payload_addr + alignment - 1) &
				~(alignment - 1);
			u64 padding = aligned_payload_addr - raw_payload_addr;

			/* Case 1: Existing block natural entry alignments perfectly match requested constraints */
			if (padding == 0 && curr->size >= aligned_size) {
				split_block(curr, aligned_size);
				curr->is_free = false;
				curr->magic = HEAP_MAGIC_ALLOCATED;
				heap->used_size +=
					(BLOCK_HEADER_SIZE + curr->size);
				*out_ptr = (void *)aligned_payload_addr;
				goto cleanup;
			}

			/* Alignment Compensation Sweep: If padding size fails to span minimum block header 
             * bounds, push the alignment target forward to allow room for a valid pad header. */
			min_required_padding =
				BLOCK_HEADER_SIZE + HEAP_MIN_BLOCK_SIZE;
			if (padding > 0 && padding < min_required_padding) {
				u64 remaining_pad =
					min_required_padding - padding;
				u64 alignment_chunks =
					(remaining_pad + alignment - 1) &
					~(alignment - 1);
				padding += alignment_chunks;
				aligned_payload_addr =
					raw_payload_addr + padding;
			}

			/* Case 2: Carve off leading padding space as an independent free block, then align allocation */
			if (padding >= min_required_padding &&
			    curr->size >= (padding + aligned_size)) {
				u64 old_total_size = curr->size;

				/* Recalibrate parent node parameters to model only the prefix pad block segment */
				curr->size = padding - BLOCK_HEADER_SIZE;

				/* Instantiate an aligned block header object directly in front of the target payload address */
				aligned_block =
					(struct heap_block
						 *)(aligned_payload_addr -
						    BLOCK_HEADER_SIZE);
				aligned_block->magic = HEAP_MAGIC_FREE;
				aligned_block->is_free = true;
				aligned_block->size = old_total_size - padding;

				/* Link the newly generated aligned block descriptor chain node into sequential positions */
				aligned_block->next = curr->next;
				aligned_block->prev = curr;
				if (curr->next) {
					curr->next->prev = aligned_block;
				}
				curr->next = aligned_block;

				/* Split excess memory out off the back of the aligned block layout */
				split_block(aligned_block, aligned_size);
				aligned_block->is_free = false;
				aligned_block->magic = HEAP_MAGIC_ALLOCATED;

				heap->used_size += (BLOCK_HEADER_SIZE +
						    aligned_block->size);
				*out_ptr = (void *)aligned_payload_addr;
				goto cleanup;
			}
		}
		curr = curr->next;
	}

	*out_ptr = NULL;
	status = ENOMEM;

cleanup:
	return status;
}

int heap_get_stats(struct heap_context *heap, u64 *used, u64 *total)
{
	int status = 0;

	if (!heap) {
		status = EINVAL;
		goto cleanup;
	}

	if (used)
		*used = heap->used_size;
	if (total)
		*total = heap->total_size;

cleanup:
	return status;
}
