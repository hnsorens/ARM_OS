#include "slab.h"
#include <modules.h>
#include <test.h>
#include <api/slab.h>
#include <api/vmm.h>
#include <api/serial_debug.h>
#include <api/mmu.h>
#include <errno.h>

IMPORT_INTERFACE_ANY(vmm, vmm);
IMPORT_INTERFACE_ANY(serial, serial);
IMPORT_INTERFACE_ANY(mmu, mmu);

EXPORT_INTERFACE(slab, SlabAllocator,
		 {
			 .create_cache = k_slab_create_cache,
			 .destroy_cache = k_slab_destroy_cache,
			 .alloc = k_slab_alloc,
			 .free = k_slab_free,
			 .shrink = k_slab_shrink,
		 });

#ifdef TESTING

TEST(Slab_CacheLifecycle)
{
	TEST_INIT();
	int status;
	u64 vmm_root;
	mmu.get_kernel_ctx(&vmm_root);

	k_slab_cache_t *cache = NULL;

	// 1. Creation and basic destruction without allocations
	status = k_slab_create_cache(vmm_root, 32, 8, &cache);
	EXPECT_EQ(status, 0);
	EXPECT_NE(cache, NULL);
	EXPECT_EQ(cache->is_allocated, true);
	EXPECT_EQ(cache->obj_size, 32);

	status = k_slab_destroy_cache(cache);
	EXPECT_EQ(status, 0);
	EXPECT_EQ(cache->is_allocated, false);

	// 2. Reject bad configurations gracefully
	status = k_slab_create_cache(vmm_root, 0, 8, &cache); // Size 0
	EXPECT_EQ(status, EINVAL);

	status = k_slab_create_cache(vmm_root, 32, 0, &cache); // Alignment 0
	EXPECT_EQ(status, EINVAL);

	status = k_slab_create_cache(vmm_root, 32, 8, NULL); // Out pointer NULL
	EXPECT_EQ(status, EINVAL);

	// Reject an oversized object that exceeds page spatial layout limits
	status = k_slab_create_cache(vmm_root, SLAB_PAGE_SIZE + 8, 8, &cache);
	EXPECT_EQ(status, EINVAL);

	// 3. Reject tearing down invalid or unallocated structures
	status = k_slab_destroy_cache(NULL);
	EXPECT_EQ(status, EINVAL);

	k_slab_cache_t fake_cache;
	fake_cache.is_allocated = false;
	status = k_slab_destroy_cache(&fake_cache);
	EXPECT_EQ(status, EINVAL);

	TEST_RESULT();
}

TEST(Slab_BasicAllocationAndFree)
{
	TEST_INIT();
	int status;
	u64 vmm_root;
	mmu.get_kernel_ctx(&vmm_root);

	k_slab_cache_t *cache = NULL;
	status = k_slab_create_cache(vmm_root, 64, 16, &cache);
	EXPECT_EQ(status, 0);

	void *obj1 = NULL;
	void *obj2 = NULL;

	// 1. Extract sequentially carved item containers
	status = k_slab_alloc(cache, &obj1);
	EXPECT_EQ(status, 0);
	EXPECT_NE(obj1, NULL);

	status = k_slab_alloc(cache, &obj2);
	EXPECT_EQ(status, 0);
	EXPECT_NE(obj2, NULL);
	EXPECT_NE(obj1, obj2); // Memory spaces must be separate

	// 2. Confirm addresses align cleanly with alignment requirements
	EXPECT_EQ((uintptr_t)obj1 % 16, 0);
	EXPECT_EQ((uintptr_t)obj2 % 16, 0);

	// 3. Drop allocations backward into the container layer
	status = k_slab_free(cache, obj1);
	EXPECT_EQ(status, 0);

	status = k_slab_free(cache, obj2);
	EXPECT_EQ(status, 0);

	// 4. Double check that freed addresses recycle back immediately (LIFO freelist behavior)
	void *obj3 = NULL;
	status = k_slab_alloc(cache, &obj3);
	EXPECT_EQ(status, 0);
	EXPECT_EQ(obj3, obj2); // LIFO recovery rule verification

	status = k_slab_free(cache, obj3);
	EXPECT_EQ(status, 0);

	status = k_slab_destroy_cache(cache);
	EXPECT_EQ(status, 0);

	TEST_RESULT();
}

TEST(Slab_SlabStateTransitions)
{
	TEST_INIT();
	int status;
	u64 vmm_root;
	mmu.get_kernel_ctx(&vmm_root);

	k_slab_cache_t *cache = NULL;
	// Set size to a value where multiple items fit exactly on one single system page
	// e.g., 1024-byte chunks mean slots_per_slab = 4 on standard 4096-byte pages
	status = k_slab_create_cache(vmm_root, 1024, 8, &cache);
	EXPECT_EQ(status, 0);

	u32 max_slots = cache->slots_per_slab;
	void *objects[max_slots + 2];

	// Verify initial layout queues stand empty
	EXPECT_EQ(cache->slabs_empty, NULL);
	EXPECT_EQ(cache->slabs_partial, NULL);
	EXPECT_EQ(cache->slabs_full, NULL);

	// 1. Single item allocation forces allocation and moves state to partial
	status = k_slab_alloc(cache, &objects[0]);
	EXPECT_EQ(status, 0);
	EXPECT_NE(cache->slabs_partial, NULL);
	EXPECT_EQ(cache->slabs_full, NULL);

	// 2. Saturate the rest of the current single page slab bounds
	for (u32 i = 1; i < max_slots; i++) {
		status = k_slab_alloc(cache, &objects[i]);
		EXPECT_EQ(status, 0);
	}

	// Slab state should switch: Partial list should drop empty, Full list claims it
	EXPECT_EQ(cache->slabs_partial, NULL);
	EXPECT_NE(cache->slabs_full, NULL);

	// 3. Allocating another item beyond bounds spawns a second physical page
	status = k_slab_alloc(cache, &objects[max_slots]);
	EXPECT_EQ(status, 0);
	EXPECT_NE(cache->slabs_partial, NULL); // Second page is partial
	EXPECT_NE(cache->slabs_full, NULL); // First page stays full

	// 4. Drop an object from the original page to pull it back down to partial state
	status = k_slab_free(cache, objects[0]);
	EXPECT_EQ(status, 0);

	// 5. Completely empty out the first slab block
	for (u32 i = 1; i < max_slots; i++) {
		status = k_slab_free(cache, objects[i]);
		EXPECT_EQ(status, 0);
	}

	// Slab should now reside perfectly on the slabs_empty queue
	EXPECT_NE(cache->slabs_empty, NULL);

	// Clean up remaining dangling entities
	status = k_slab_free(cache, objects[max_slots]);
	EXPECT_EQ(status, 0);

	status = k_slab_destroy_cache(cache);
	EXPECT_EQ(status, 0);

	TEST_RESULT();
}

TEST(Slab_ShrinkAndReclaim)
{
	TEST_INIT();
	int status;
	u64 vmm_root;
	mmu.get_kernel_ctx(&vmm_root);

	k_slab_cache_t *cache = NULL;
	status = k_slab_create_cache(vmm_root, 256, 8, &cache);
	EXPECT_EQ(status, 0);

	void *obj = NULL;
	status = k_slab_alloc(cache, &obj);
	EXPECT_EQ(status, 0);

	// Release immediately to leave a fully configured but vacant page sitting inside empty
	status = k_slab_free(cache, obj);
	EXPECT_EQ(status, 0);
	EXPECT_NE(cache->slabs_empty, NULL);

	// 1. Evict completely vacant memory back to underlying architecture management
	status = k_slab_shrink(cache);
	EXPECT_EQ(status, 0);
	EXPECT_EQ(cache->slabs_empty, NULL); // Checked back out cleanly

	status = k_slab_destroy_cache(cache);
	EXPECT_EQ(status, 0);

	TEST_RESULT();
}

TEST(Slab_ExhaustionAndBoundaries)
{
	TEST_INIT();
	int status;
	u64 vmm_root;
	mmu.get_kernel_ctx(&vmm_root);

	k_slab_cache_t *cache = NULL;
	// Massive allocations requiring a new descriptor node for almost every object context
	status = k_slab_create_cache(vmm_root, 2048, 8, &cache);
	EXPECT_EQ(status, 0);

// MAX_SLAB_DESCRIPTORS is set to 128 in your module code
#define DESCRIPTOR_BOUNDS 135
	void *tracked_allocations[DESCRIPTOR_BOUNDS];
	int successfully_allocated = 0;

	// 1. Force structural failure by consuming the preallocated global k_slab_t descriptor pool
	for (int i = 0; i < DESCRIPTOR_BOUNDS; i++) {
		status = k_slab_alloc(cache, &tracked_allocations[i]);
		if (status == 0) {
			successfully_allocated++;
		} else {
			// Must handle out-of-descriptors with standard error codes
			EXPECT_EQ(status, ENOMEM);
			break;
		}
	}

	// 2. Recover seamlessly from exhaustion state
	for (int i = 0; i < successfully_allocated; i++) {
		status = k_slab_free(cache, tracked_allocations[i]);
		EXPECT_EQ(status, 0);
	}

	status = k_slab_destroy_cache(cache);
	EXPECT_EQ(status, 0);

	TEST_RESULT();
}

TEST(Slab_RobustnessEdgeCases)
{
	TEST_INIT();
	int status;
	u64 vmm_root;
	mmu.get_kernel_ctx(&vmm_root);

	k_slab_cache_t *cache = NULL;
	status = k_slab_create_cache(vmm_root, 128, 8, &cache);
	EXPECT_EQ(status, 0);

	// 1. Verify invalid references are caught during allocations
	status = k_slab_alloc(NULL, NULL);
	EXPECT_EQ(status, EINVAL);

	void *out_ptr = NULL;
	status = k_slab_alloc(NULL, &out_ptr);
	EXPECT_EQ(status, EINVAL);

	status = k_slab_alloc(cache, NULL);
	EXPECT_EQ(status, EINVAL);

	// 2. Reject untracked/corrupted target pointers passed to free hooks
	u64 foreign_address = 0xDEADBEEF0000UL;
	status = k_slab_free(cache, (void *)foreign_address);
	EXPECT_EQ(status, EINVAL);

	status = k_slab_free(NULL, (void *)foreign_address);
	EXPECT_EQ(status, EINVAL);

	// 3. Edge-Case size configuration checks (checking that the internal minimum size matches sizeof(u32))
	k_slab_cache_t *small_cache = NULL;
	status = k_slab_create_cache(vmm_root, 1, 1,
				     &small_cache); // 1-byte elements
	EXPECT_EQ(status, 0);
	EXPECT_LE(
		sizeof(u32),
		small_cache
			->obj_size); // Internal scaling expands layout minimum size to preserve the freelist link footprint

	status = k_slab_destroy_cache(small_cache);
	EXPECT_EQ(status, 0);
	status = k_slab_destroy_cache(cache);
	EXPECT_EQ(status, 0);

	TEST_RESULT();
}

#endif
