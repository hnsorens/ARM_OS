/**
 * @file slab.c
 * @brief Core Architecture and Page-Layer Tracking Engine for the Slab Allocator.
 * * Implements low-overhead fixed-size block caching. Backing pages use an intrusive
 * bit-packed header pointing to cell sequences linked directly via internal payload space,
 * achieving true $O(1)$ allocations and returns without external memory tracking tables.
 */

#include "slab.h"
#include <api/vmm.h>
#include <modules.h>
#include <utils.h>
#include <errno.h>

/* --- Core Module Linkage Hooks --- */
EXTERN_IMPORT_INTERFACE(vmm, vmm);

#define SLAB_LIST_END \
	0xFFFFFFFF /**< Sentinel value marking the terminal node of an internal slab freelist chain */

/**
 * @struct k_slab
 * @brief Intrusive page tracker positioned at byte 0 of every virtual page allocated.
 * * Tracks available object availability maps natively inside the underlying structural page,
 * threaded seamlessly onto the parent cache's doubly-linked state tracking lists.
 */
struct k_slab {
	u32 free_count; /**< Number of available object slots sitting idle on this page */
	u32 next_free_slot; /**< Index locating the next unallocated element array slot container */
	struct k_slab *
		next; /**< Sibling forward pointer inside current tracking queue link list */
	struct k_slab *
		prev; /**< Sibling ancestral pointer inside current tracking queue link list */
};

/**
 * @brief Internal Helper: Computes the memory address location where cell payloads begin.
 * @param[in] slab Direct reference tracking the targeted physical page structure header.
 * @param[in] alignment Math alignment factor enforced to satisfy architectural rules.
 * @return uintptr_t Memory address pointing exactly to the first valid allocation unit slot.
 */
static inline uintptr_t get_payload_start(k_slab_t *slab, u64 alignment)
{
	uintptr_t header_end = (uintptr_t)slab + sizeof(k_slab_t);
	return (header_end + (alignment - 1)) & ~(alignment - 1);
}

/**
 * @brief Internal Helper: Enqueues a slab page onto the head of a target tracking list.
 * @param[in,out] head Master state list reference node tracking the chosen queue (empty, partial, full).
 * @param[in]     slab Target tracking slab structure node to splice into position.
 */
static void enqueue_slab(k_slab_t **head, k_slab_t *slab)
{
	slab->next = *head;
	slab->prev = NULL;
	if (*head) {
		(*head)->prev = slab;
	}
	*head = slab;
}

/**
 * @brief Internal Helper: Unlinks a slab page tracking node from an active link list sequence.
 * @param[in,out] head Master state list reference node tracking the chosen queue.
 * @param[in]     slab Target tracking slab structure node to extract from the sequence.
 */
static void dequeue_slab(k_slab_t **head, k_slab_t *slab)
{
	if (slab->prev) {
		slab->prev->next = slab->next;
	} else if (*head == slab) {
		*head = slab->next;
	}
	if (slab->next) {
		slab->next->prev = slab->prev;
	}
	slab->next = NULL;
	slab->prev = NULL;
}

int k_slab_create_cache(u64 root, u64 obj_size, u64 alignment,
			k_slab_cache_t **out_cache)
{
	if (obj_size == 0 || alignment == 0 || !out_cache) {
		return EINVAL;
	}

	/* Architectural validation guard: Enforce power-of-two spatial boundary profiles */
	if ((alignment & (alignment - 1)) != 0) {
		return EINVAL;
	}

	/* Up-align requested slot size dimensions according to chosen architectural constraints */
	u64 aligned_obj_size = (obj_size + (alignment - 1)) & ~(alignment - 1);

	/* Security Constraint: Enforce 4-byte minimum slot floors to safely embed unallocated node index chains */
	if (aligned_obj_size < sizeof(u32)) {
		aligned_obj_size = sizeof(u32);
	}

	/* Dry-run offset simulation checking total spacing layout constraints within page windows */
	uintptr_t dummy_slab = 0;
	uintptr_t header_end = dummy_slab + sizeof(k_slab_t);
	uintptr_t localized_payload_start = (header_end + (alignment - 1)) &
					    ~(alignment - 1);

	/* Fail immediately if aligned object configurations exceed entire raw page boundaries */
	if (aligned_obj_size > (SLAB_PAGE_SIZE - localized_payload_start)) {
		return EINVAL;
	}

	/* Request master control cache descriptor allocation from the virtual page mapping pool */
	u64 cache_vaddr = 0xFFFF900000000000;
	int status = vmm.allocate(root, &cache_vaddr, SLAB_PAGE_SIZE, 0x713,
				  VMM_REGION_HEAP);
	if (status != 0) {
		return ENOMEM;
	}

	k_slab_cache_t *cache = (k_slab_cache_t *)cache_vaddr;
	kmemset(cache, 0, sizeof(k_slab_cache_t));

	cache->root = root;
	cache->obj_size = aligned_obj_size;
	cache->alignment = alignment;

	/* Compute accurate capacity limits accounting for structural header and alignment gaps */
	cache->slots_per_slab =
		(SLAB_PAGE_SIZE - localized_payload_start) / aligned_obj_size;

	if (cache->slots_per_slab == 0) {
		vmm.free(root, cache_vaddr, SLAB_PAGE_SIZE);
		return EINVAL;
	}

	cache->slabs_full = NULL;
	cache->slabs_partial = NULL;
	cache->slabs_empty = NULL;
	cache->is_allocated = true;

	*out_cache = cache;
	return 0;
}

int k_slab_alloc(k_slab_cache_t *cache, void **out_obj)
{
	if (!cache || !cache->is_allocated || !out_obj) {
		return EINVAL;
	}

	k_slab_t *slab = NULL;
	bool fresh_or_empty = false;

	/* Search Strategy: Prioritize partial pages to minimize fragmentation and optimize page reuse */
	if (cache->slabs_partial) {
		slab = cache->slabs_partial;
	} else if (cache->slabs_empty) {
		/* Reallocate an existing unmapped empty page structure, moving it to an active state */
		slab = cache->slabs_empty;
		dequeue_slab(&cache->slabs_empty, slab);
		fresh_or_empty = true;
	} else {
		/* Fallback: Allocate a brand-new pristine physical memory page window via the VMM */
		u64 data_vaddr = 0xFFFF900000000000;
		int status = vmm.allocate(cache->root, &data_vaddr,
					  SLAB_PAGE_SIZE, 0x713,
					  VMM_REGION_HEAP);
		if (status != 0) {
			return ENOMEM;
		}

		slab = (k_slab_t *)data_vaddr;
		slab->free_count = (u32)cache->slots_per_slab;
		slab->next_free_slot = 0;
		slab->next = NULL;
		slab->prev = NULL;

		/* Intrusively initialize the free list chain directly within unallocated object slots */
		uintptr_t payload_base =
			get_payload_start(slab, cache->alignment);
		for (u32 i = 0; i < cache->slots_per_slab - 1; i++) {
			u32 *next_link =
				(u32 *)(payload_base + (i * cache->obj_size));
			*next_link =
				i +
				1; // Direct linear index mapping serialization
		}
		u32 *last_link =
			(u32 *)(payload_base + ((cache->slots_per_slab - 1) *
						cache->obj_size));
		*last_link = SLAB_LIST_END;

		fresh_or_empty = true;
	}

	/* Extract the current object cell slot located at the head of the tracking index chain */
	uintptr_t payload_start = get_payload_start(slab, cache->alignment);
	uintptr_t alloc_addr =
		payload_start + (slab->next_free_slot * cache->obj_size);

	/* Advance the internal tracking head index forward to target the subsequent unallocated slot link */
	slab->next_free_slot = *(u32 *)alloc_addr;
	slab->free_count--;

	*out_obj = (void *)alloc_addr;

	/* Route the updated page tracking node across active lists matching new saturation metrics */
	if (fresh_or_empty) {
		if (slab->free_count == 0) {
			enqueue_slab(&cache->slabs_full, slab);
		} else {
			enqueue_slab(&cache->slabs_partial, slab);
		}
	} else {
		/* If a previously partial page becomes completely saturated, advance it into the full queue */
		if (slab->free_count == 0) {
			dequeue_slab(&cache->slabs_partial, slab);
			enqueue_slab(&cache->slabs_full, slab);
		}
	}

	return 0;
}

int k_slab_free(k_slab_cache_t *cache, void *obj)
{
	if (!cache || !obj || !cache->is_allocated) {
		return EINVAL;
	}

	uintptr_t obj_addr = (uintptr_t)obj;

	/* Extrapolate base address location of page container via binary alignment rounding passes */
	k_slab_t *slab =
		(k_slab_t *)(obj_addr & ~(uintptr_t)(SLAB_PAGE_SIZE - 1));
	uintptr_t payload_start = get_payload_start(slab, cache->alignment);

	/* Security check: Reject addresses violating structural interior object boundaries */
	if (obj_addr < payload_start ||
	    obj_addr >= ((uintptr_t)slab + SLAB_PAGE_SIZE)) {
		return EINVAL;
	}

	/* Derive index offset parameters matching exact object spacing distances */
	u32 slot_idx = (u32)((obj_addr - payload_start) / cache->obj_size);
	if (slot_idx >= cache->slots_per_slab) {
		return EINVAL;
	}

	/* Conditionally extract from active queues to update state grouping memberships */
	if (slab->free_count == 0) {
		dequeue_slab(&cache->slabs_full, slab);
	} else {
		/* Extract from partial queue if this return operation triggers a complete transition back to empty */
		if (slab->free_count + 1 == cache->slots_per_slab) {
			dequeue_slab(&cache->slabs_partial, slab);
		}
	}

	/* Embed the old index link directly within the payload cell to append it back onto the list */
	*(u32 *)obj_addr = slab->next_free_slot;
	slab->next_free_slot = slot_idx;
	slab->free_count++;

	/* Re-sort the updated slab into the appropriate category pool stream matching remaining element balances */
	if (slab->free_count == cache->slots_per_slab) {
		enqueue_slab(&cache->slabs_empty, slab);
	} else if (slab->free_count - 1 == 0) {
		enqueue_slab(&cache->slabs_partial, slab);
	}

	return 0;
}

int k_slab_shrink(k_slab_cache_t *cache)
{
	if (!cache || !cache->is_allocated) {
		return EINVAL;
	}

	/* Isolate and capture the entire empty link list head node sequence */
	k_slab_t *curr = cache->slabs_empty;
	cache->slabs_empty = NULL;

	/* Loop-unmap all completely vacant pages back into virtual memory management structures */
	while (curr) {
		k_slab_t *next = curr->next;
		vmm.free(cache->root, (u64)curr, SLAB_PAGE_SIZE);
		curr = next;
	}

	return 0;
}

int k_slab_destroy_cache(k_slab_cache_t *cache)
{
	if (!cache || !cache->is_allocated) {
		return EINVAL;
	}

	u64 root = cache->root;
	cache->is_allocated = false;

	/* Unmap and purge all pages registered under the full cache tracking list */
	k_slab_t *curr = cache->slabs_full;
	while (curr) {
		k_slab_t *next = curr->next;
		vmm.free(root, (u64)curr, SLAB_PAGE_SIZE);
		curr = next;
	}
	cache->slabs_full = NULL;

	/* Unmap and purge all pages registered under the partial cache tracking list */
	curr = cache->slabs_partial;
	while (curr) {
		k_slab_t *next = curr->next;
		vmm.free(root, (u64)curr, SLAB_PAGE_SIZE);
		curr = next;
	}
	cache->slabs_partial = NULL;

	/* Evict and drop all remaining pristine unallocated page layers safely */
	k_slab_shrink(cache);
	return 0;
}
