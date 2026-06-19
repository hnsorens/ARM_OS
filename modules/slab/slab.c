#include "slab.h"
#include <api/vmm.h>
#include <modules.h>
#include <utils.h>
#include <errno.h>

EXTERN_IMPORT_INTERFACE(vmm, vmm);

#define SLAB_LIST_END 0xFFFFFFFF

/* * INTRUSIVE HEADER: Embedded at byte-offset 0 of every backing page.
 * Managed as a doubly-linked list to achieve clean O(1) transitions.
 */
struct k_slab {
	u32 free_count;
	u32 next_free_slot;
	struct k_slab *next;
	struct k_slab *prev;
};

/* Helper: Inline utility to calculate the strictly aligned start of object slots */
static inline uintptr_t get_payload_start(k_slab_t *slab, u64 alignment)
{
	uintptr_t header_end = (uintptr_t)slab + sizeof(k_slab_t);
	return (header_end + (alignment - 1)) & ~(alignment - 1);
}

/* Doubly-linked list insertion: Constant time O(1) */
static void enqueue_slab(k_slab_t **head, k_slab_t *slab)
{
	slab->next = *head;
	slab->prev = NULL;
	if (*head) {
		(*head)->prev = slab;
	}
	*head = slab;
}

/* Doubly-linked list extraction: Constant time O(1) */
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
	if ((alignment & (alignment - 1)) != 0) {
		return EINVAL;
	}

	u64 aligned_obj_size = (obj_size + (alignment - 1)) & ~(alignment - 1);
	if (aligned_obj_size < sizeof(u32)) {
		aligned_obj_size = sizeof(u32);
	}

	/* Track dynamic overhead to verify total spacing constraints */
	uintptr_t dummy_slab = 0;
	uintptr_t header_end = dummy_slab + sizeof(k_slab_t);
	uintptr_t localized_payload_start = (header_end + (alignment - 1)) &
					    ~(alignment - 1);

	if (aligned_obj_size > (SLAB_PAGE_SIZE - localized_payload_start)) {
		return EINVAL;
	}

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

	/* Calculate precise slots remaining after header + alignment rounding spacing gap */
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

	if (cache->slabs_partial) {
		/* FIX: Do not pull from the list yet. If it stays partial, we avoid link churn */
		slab = cache->slabs_partial;
	} else if (cache->slabs_empty) {
		slab = cache->slabs_empty;
		dequeue_slab(&cache->slabs_empty, slab);
		fresh_or_empty = true;
	} else {
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

		/* FIX: Calculate next list chains using strictly aligned payload boundaries */
		uintptr_t payload_base =
			get_payload_start(slab, cache->alignment);
		for (u32 i = 0; i < cache->slots_per_slab - 1; i++) {
			u32 *next_link =
				(u32 *)(payload_base + (i * cache->obj_size));
			*next_link = i + 1;
		}
		u32 *last_link =
			(u32 *)(payload_base + ((cache->slots_per_slab - 1) *
						cache->obj_size));
		*last_link = SLAB_LIST_END;

		fresh_or_empty = true;
	}

	/* Pop allocation container index off the inner stack chain */
	uintptr_t payload_start = get_payload_start(slab, cache->alignment);
	uintptr_t alloc_addr =
		payload_start + (slab->next_free_slot * cache->obj_size);

	slab->next_free_slot = *(u32 *)alloc_addr;
	slab->free_count--;

	*out_obj = (void *)alloc_addr;

	/* Handle transitions safely without list-corruption churn */
	if (fresh_or_empty) {
		if (slab->free_count == 0) {
			enqueue_slab(&cache->slabs_full, slab);
		} else {
			enqueue_slab(&cache->slabs_partial, slab);
		}
	} else {
		/* It was already in partial. If it filled up completely, move it to full */
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
	k_slab_t *slab =
		(k_slab_t *)(obj_addr & ~(uintptr_t)(SLAB_PAGE_SIZE - 1));
	uintptr_t payload_start = get_payload_start(slab, cache->alignment);

	if (obj_addr < payload_start ||
	    obj_addr >= ((uintptr_t)slab + SLAB_PAGE_SIZE)) {
		return EINVAL;
	}

	u32 slot_idx = (u32)((obj_addr - payload_start) / cache->obj_size);
	if (slot_idx >= cache->slots_per_slab) {
		return EINVAL;
	}

	/* Unconditionally manage clean removal only if it changes tracking category pools */
	if (slab->free_count == 0) {
		dequeue_slab(&cache->slabs_full, slab);
	} else {
		if (slab->free_count + 1 == cache->slots_per_slab) {
			dequeue_slab(&cache->slabs_partial, slab);
		}
	}

	*(u32 *)obj_addr = slab->next_free_slot;
	slab->next_free_slot = slot_idx;
	slab->free_count++;

	/* Re-sort into appropriate allocation streams */
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

	k_slab_t *curr = cache->slabs_empty;
	cache->slabs_empty = NULL;

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

	k_slab_t *curr = cache->slabs_full;
	while (curr) {
		k_slab_t *next = curr->next;
		vmm.free(root, (u64)curr, SLAB_PAGE_SIZE);
		curr = next;
	}
	cache->slabs_full = NULL;

	curr = cache->slabs_partial;
	while (curr) {
		k_slab_t *next = curr->next;
		vmm.free(root, (u64)curr, SLAB_PAGE_SIZE);
		curr = next;
	}
	cache->slabs_partial = NULL;

	k_slab_shrink(cache);
	return 0;
}
