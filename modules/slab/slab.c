#include "slab.h"
#include "../../include/api/vmm.h"
#include "../modules.h"
#include "../utils.h"

EXTERN_IMPORT_INTERFACE(vmm, vmm);

#define MAX_SLAB_DESCRIPTORS 128

/* Central storage matrix tracking active object caches within the kernel subsystem */
static k_slab_cache_t s_caches[SLAB_MAX_CACHES];

/* Pre-allocated tracking structure pool managing metadata descriptors independently from raw pages */
struct k_slab {
	void *page_base; /* Pointer to the start of the virtual page */
	uint32_t free_count; /* Remaining vacant object slots inside this slab */
	uint32_t next_free_slot; /* Index of the first available slot */
	struct k_slab *next; /* Pointer to the next sibling slab tracker */
};

static k_slab_t s_slab_pool[MAX_SLAB_DESCRIPTORS];

/* Helper: Allocates an uninitialized metadata node out of the tracking pool */
static k_slab_t *alloc_slab_descriptor(void)
{
	size_t i;
	for (i = 0; i < MAX_SLAB_DESCRIPTORS; i++) {
		if (s_slab_pool[i].page_base == NULL) {
			return &s_slab_pool[i];
		}
	}
	return NULL;
}

/* Helper: Recycles a metadata node back into the tracking pool */
static void free_slab_descriptor(k_slab_t *s)
{
	if (s) {
		kmemset(s, 0, sizeof(k_slab_t));
	}
}

/* Helper: Moves a slab out of one list and inserts it at the head of another */
static void move_slab(k_slab_t **from_list, k_slab_t **to_list, k_slab_t *s)
{
	k_slab_t *curr;

	if (!from_list || !to_list || !s)
		return;

	if (*from_list == s) {
		*from_list = s->next;
	} else {
		curr = *from_list;
		while (curr && curr->next != s) {
			curr = curr->next;
		}
		if (curr)
			curr->next = s->next;
	}

	s->next = *to_list;
	*to_list = s;
}

/* Pre-allocates and registers a brand-new fixed-size object cache store, returning a direct pointer handle */
k_status_t k_slab_create_cache(paddr_t root, size_t obj_size, size_t alignment,
			       k_slab_cache_t **out_cache)
{
	k_status_t status = K_STATUS_OK;
	k_slab_cache_t *cache = NULL;
	size_t aligned_obj_size;
	size_t i;

	if (obj_size == 0 || alignment == 0 || !out_cache) {
		status = K_STATUS_INVALID_ARG;
		goto cleanup;
	}

	for (i = 0; i < SLAB_MAX_CACHES; i++) {
		if (!s_caches[i].is_allocated) {
			cache = &s_caches[i];
			break;
		}
	}

	if (!cache) {
		status = K_STATUS_OUT_OF_MEMORY;
		goto cleanup;
	}

	aligned_obj_size = (obj_size + (alignment - 1)) & ~(alignment - 1);
	if (aligned_obj_size < sizeof(uint32_t)) {
		aligned_obj_size = sizeof(uint32_t);
	}

	if (aligned_obj_size > (SLAB_PAGE_SIZE - alignment)) {
		status = K_STATUS_INVALID_ARG;
		goto cleanup;
	}

	cache->root = root;
	cache->obj_size = aligned_obj_size;
	cache->alignment = alignment;
	cache->slots_per_slab = SLAB_PAGE_SIZE / aligned_obj_size;
	cache->slabs_full = NULL;
	cache->slabs_partial = NULL;
	cache->slabs_empty = NULL;
	cache->is_allocated = true;

	*out_cache = cache;

cleanup:
	return status;
}

/* Destroys an object cache bucket using its direct handle and drops all mapped pages back to the VMM */
k_status_t k_slab_destroy_cache(k_slab_cache_t *cache)
{
	k_status_t status = K_STATUS_OK;
	k_slab_t *curr;
	k_slab_t *next;
	k_slab_t *lists[3];
	size_t i;

	if (!cache || !cache->is_allocated) {
		status = K_STATUS_INVALID_ARG;
		goto cleanup;
	}

	lists[0] = cache->slabs_full;
	lists[1] = cache->slabs_partial;
	lists[2] = cache->slabs_empty;

	for (i = 0; i < 3; i++) {
		curr = lists[i];
		while (curr) {
			next = curr->next;
			vmm.free(cache->root, (vaddr_t)curr->page_base,
				 SLAB_PAGE_SIZE);
			free_slab_descriptor(curr);
			curr = next;
		}
	}

	kmemset(cache, 0, sizeof(k_slab_cache_t));

cleanup:
	return status;
}

/* Fetches a single pre-carved object slot from the requested slab cache using its direct handle */
k_status_t k_slab_alloc(k_slab_cache_t *cache, void **out_obj)
{
	k_status_t status = K_STATUS_OK;
	k_slab_t *s = NULL;
	vaddr_t vaddr = 0;
	uintptr_t base;
	uintptr_t alloc_addr;
	size_t i;

	if (!cache || !cache->is_allocated || !out_obj) {
		status = K_STATUS_INVALID_ARG;
		goto cleanup;
	}

	if (cache->slabs_partial) {
		s = cache->slabs_partial;
	} else if (cache->slabs_empty) {
		s = cache->slabs_empty;
		move_slab(&cache->slabs_empty, &cache->slabs_partial, s);
	} else {
		status = vmm.allocate(cache->root, &vaddr, SLAB_PAGE_SIZE,
				      MMU_READ | MMU_WRITE, VMM_REGION_HEAP);
		if (k_error(status)) {
			goto cleanup;
		}

		s = alloc_slab_descriptor();
		if (!s) {
			vmm.free(cache->root, vaddr, SLAB_PAGE_SIZE);
			status = K_STATUS_OUT_OF_MEMORY;
			goto cleanup;
		}

		s->page_base = (void *)vaddr;
		s->free_count = cache->slots_per_slab;
		s->next_free_slot = 0;
		s->next = NULL;

		base = (uintptr_t)s->page_base;
		for (i = 0; i < cache->slots_per_slab - 1; i++) {
			uint32_t *next_link =
				(uint32_t *)(base + (i * cache->obj_size));
			*next_link = i + 1;
		}
		uint32_t *last_link =
			(uint32_t *)(base + ((cache->slots_per_slab - 1) *
					     cache->obj_size));
		*last_link = 0xFFFFFFFF;

		s->next = cache->slabs_partial;
		cache->slabs_partial = s;
	}

	alloc_addr =
		(uintptr_t)s->page_base + (s->next_free_slot * cache->obj_size);
	s->next_free_slot = *(uint32_t *)alloc_addr;
	s->free_count--;

	if (s->free_count == 0) {
		move_slab(&cache->slabs_partial, &cache->slabs_full, s);
	}

	*out_obj = (void *)alloc_addr;

cleanup:
	return status;
}

/* Returns an active allocated object slot container back into its native cache using its direct handle */
k_status_t k_slab_free(k_slab_cache_t *cache, void *obj)
{
	k_status_t status = K_STATUS_OK;
	uintptr_t obj_addr;
	uintptr_t page_mask;
	void *page_base;
	k_slab_t *s = NULL;
	k_slab_t **source_list = NULL;
	k_slab_t *lists[2];
	k_slab_t **refs[2];
	uint32_t slot_idx;
	size_t i;

	if (!cache || !obj || !cache->is_allocated) {
		status = K_STATUS_INVALID_ARG;
		goto cleanup;
	}

	obj_addr = (uintptr_t)obj;
	page_mask = ~((uintptr_t)SLAB_PAGE_SIZE - 1);
	page_base = (void *)(obj_addr & page_mask);

	lists[0] = cache->slabs_partial;
	lists[1] = cache->slabs_full;
	refs[0] = &cache->slabs_partial;
	refs[1] = &cache->slabs_full;

	for (i = 0; i < 2; i++) {
		k_slab_t *curr = lists[i];
		while (curr) {
			if (curr->page_base == page_base) {
				s = curr;
				source_list = refs[i];
				break;
			}
			curr = curr->next;
		}
		if (s)
			break;
	}

	if (!s) {
		status = K_STATUS_NOT_FOUND;
		goto cleanup;
	}

	slot_idx = (obj_addr - (uintptr_t)s->page_base) / cache->obj_size;

	*(uint32_t *)obj_addr = s->next_free_slot;
	s->next_free_slot = slot_idx;
	s->free_count++;

	if (s->free_count == cache->slots_per_slab) {
		move_slab(source_list, &cache->slabs_empty, s);
	} else if (s->free_count == 1 && source_list == &cache->slabs_full) {
		move_slab(&cache->slabs_full, &cache->slabs_partial, s);
	}

cleanup:
	return status;
}

/* Evicts completely vacant slabs from the cache to reclaim system pages using its direct handle */
k_status_t k_slab_shrink(k_slab_cache_t *cache)
{
	k_status_t status = K_STATUS_OK;
	k_slab_t *curr;
	k_slab_t *next;

	if (!cache || !cache->is_allocated) {
		status = K_STATUS_INVALID_ARG;
		goto cleanup;
	}

	curr = cache->slabs_empty;
	cache->slabs_empty = NULL;

	while (curr) {
		next = curr->next;
		vmm.free(cache->root, (vaddr_t)curr->page_base, SLAB_PAGE_SIZE);
		free_slab_descriptor(curr);
		curr = next;
	}

cleanup:
	return status;
}
