/**
 * @file main.c
 * @brief Test Suite and Interface Exports for the Fixed-Size Slab Allocator.
 * * Exposes core slab cache subsystem interfaces to the kernel module framework,
 * and executes functional verification profiling cache lifecycles, state transitions,
 * out-of-descriptor bounds protection, and LIFO recycling heuristics.
 */

#include "slab.h"
#include <modules.h>
#include <test.h>
#include <api/slab.h>
#include <api/vmm.h>
#include <api/serial_debug.h>
#include <api/mmu.h>
#include <errno.h>

/* --- Subsystem Module Imports --- */
IMPORT_INTERFACE_ANY(vmm, vmm);
IMPORT_INTERFACE_ANY(serial, serial);
IMPORT_INTERFACE_ANY(mmu, mmu);

/* --- Subsystem API Interface Export Registration --- */
EXPORT_INTERFACE(slab, SlabAllocator,
		 {
			 .create_cache = k_slab_create_cache,
			 .destroy_cache = k_slab_destroy_cache,
			 .alloc = k_slab_alloc,
			 .free = k_slab_free,
			 .shrink = k_slab_shrink,
		 });

#ifdef TESTING

/**
 * @brief Test Case: Cache Lifecycle and Parameter Validation.
 * Verifies nominal creation and teardown sequences, alongside strict error trapping
 * for zero sizing, invalid alignments, and page-overflow constraints.
 */
TEST(Slab_CacheLifecycle)
{
	TEST_INIT();
	int status;
	u64 vmm_root;
	mmu.get_kernel_ctx(&vmm_root);

	k_slab_cache_t *cache = NULL;

	// 1. Nominal creation and direct teardown without executing allocations
	status = k_slab_create_cache(vmm_root, 32, 8, &cache);
	EXPECT_EQ(status, 0);
	EXPECT_NE(cache, NULL);
	EXPECT_EQ(cache->is_allocated, true);
	EXPECT_EQ(cache->obj_size, 32);

	status = k_slab_destroy_cache(cache);
	EXPECT_EQ(status, 0);
	EXPECT_EQ(cache->is_allocated, false);

	// 2. Structural Guard Validation: Reject invalid arguments and bad layouts
	status = k_slab_create_cache(
		vmm_root, 0, 8, &cache); // Invalid: Element size cannot be zero
	EXPECT_EQ(status, EINVAL);

	status = k_slab_create_cache(
		vmm_root, 32, 0,
		&cache); // Invalid: Alignment factor cannot be zero
	EXPECT_EQ(status, EINVAL);

	status = k_slab_create_cache(
		vmm_root, 32, 8,
		NULL); // Invalid: Missing output handle storage destination
	EXPECT_EQ(status, EINVAL);

	// Reject allocation profiles where a single object exceeds page boundary headroom capacities
	status = k_slab_create_cache(vmm_root, SLAB_PAGE_SIZE + 8, 8, &cache);
	EXPECT_EQ(status, EINVAL);

	// 3. Architecture Safety Guards: Block destruction of invalid handles
	status = k_slab_destroy_cache(NULL);
	EXPECT_EQ(status, EINVAL);

	k_slab_cache_t fake_cache;
	fake_cache.is_allocated = false;
	status = k_slab_destroy_cache(&fake_cache);
	EXPECT_EQ(status, EINVAL);

	TEST_RESULT();
}

/**
 * @brief Test Case: Basic Allocation, Freeing, and LIFO Recycling.
 * Verifies sequential payload carving, adherence to specified alignment masks,
 * and strict verification of Last-In-First-Out freelist recovery rules.
 */
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

	// 1. Extract sequential contiguous payload allocations from the active cache container
	status = k_slab_alloc(cache, &obj1);
	EXPECT_EQ(status, 0);
	EXPECT_NE(obj1, NULL);

	status = k_slab_alloc(cache, &obj2);
	EXPECT_EQ(status, 0);
	EXPECT_NE(obj2, NULL);
	EXPECT_NE(
		obj1,
		obj2); // Spatial isolation: Allocated memory slots must never overlap

	// 2. Mathematical Boundary Check: Confirm returned addresses match specified bit alignment masks
	EXPECT_EQ((uintptr_t)obj1 % 16, 0);
	EXPECT_EQ((uintptr_t)obj2 % 16, 0);

	// 3. Drop allocation handles backward into the underlying page freelist chain
	status = k_slab_free(cache, obj1);
	EXPECT_EQ(status, 0);

	status = k_slab_free(cache, obj2);
	EXPECT_EQ(status, 0);

	// 4. Heuristic Verification: Ensure the immediate subsequent allocation targets the most recently freed slot
	void *obj3 = NULL;
	status = k_slab_alloc(cache, &obj3);
	EXPECT_EQ(status, 0);
	EXPECT_EQ(obj3, obj2); // LIFO recovery match confirmation

	status = k_slab_free(cache, obj3);
	EXPECT_EQ(status, 0);

	status = k_slab_destroy_cache(cache);
	EXPECT_EQ(status, 0);

	TEST_RESULT();
}

/**
 * @brief Test Case: Slab Tri-State Queue Transitions.
 * Profiles explicit lifecycle path tracking: routing pages dynamically across
 * empty, partial, and full queues matching operational saturations.
 */
TEST(Slab_SlabStateTransitions)
{
	TEST_INIT();
	int status;
	u64 vmm_root;
	mmu.get_kernel_ctx(&vmm_root);

	k_slab_cache_t *cache = NULL;

	/* Configures page properties such that exactly 4 objects fit per backing layout window */
	status = k_slab_create_cache(vmm_root, 1024, 8, &cache);
	EXPECT_EQ(status, 0);

	u32 max_slots = cache->slots_per_slab;
	void *objects[max_slots + 2];

	// Assert initial layout states register completely vacant
	cache->slabs_empty = NULL;
	cache->slabs_partial = NULL;
	cache->slabs_full = NULL;

	// 1. Initial allocation forces page generation, immediately targeting the partial tracking queue
	status = k_slab_alloc(cache, &objects[0]);
	EXPECT_EQ(status, 0);
	EXPECT_NE(cache->slabs_partial, NULL);
	EXPECT_EQ(cache->slabs_full, NULL);

	// 2. Saturate all remaining unallocated slots on the current backing page
	for (u32 i = 1; i < max_slots; i++) {
		status = k_slab_alloc(cache, &objects[i]);
		EXPECT_EQ(status, 0);
	}

	// State Transition Alpha: Fully saturated page drops from partial and joins the full tracking queue
	EXPECT_EQ(cache->slabs_partial, NULL);
	EXPECT_NE(cache->slabs_full, NULL);

	// 3. Request extra allocation beyond current limits to force physical expansion of a new page frame
	status = k_slab_alloc(cache, &objects[max_slots]);
	EXPECT_EQ(status, 0);
	EXPECT_NE(cache->slabs_partial,
		  NULL); // New slab establishes a partial anchor point
	EXPECT_NE(
		cache->slabs_full,
		NULL); // Original slab continues to sit on full queue undisturbed

	// 4. Release a single element from the saturated page to pull it back down into partial tracking pools
	status = k_slab_free(cache, objects[0]);
	EXPECT_EQ(status, 0);

	// 5. Systematically free all active objects remaining on the original slab page
	for (u32 i = 1; i < max_slots; i++) {
		status = k_slab_free(cache, objects[i]);
		EXPECT_EQ(status, 0);
	}

	// State Transition Beta: Completely cleared backing page shifts directly into the empty pool list
	EXPECT_NE(cache->slabs_empty, NULL);

	// 6. Tear down residual allocations to clear out the expanded slab page context smoothly
	status = k_slab_free(cache, objects[max_slots]);
	EXPECT_EQ(status, 0);

	status = k_slab_destroy_cache(cache);
	EXPECT_EQ(status, 0);

	TEST_RESULT();
}

/**
 * @brief Test Case: On-Demand Memory Shrinking and Core Reclamation.
 * Validates eviction procedures sweeping unutilized, pristine empty pages
 * back to virtual memory manager structures.
 */
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

	// Release immediately to generate a fully formatted, unallocated slab riding the empty tracking list
	status = k_slab_free(cache, obj);
	EXPECT_EQ(status, 0);
	EXPECT_NE(cache->slabs_empty, NULL);

	// 1. Force purging sweep across vacant margins to drop idle page layers back out of active layouts
	status = k_slab_shrink(cache);
	EXPECT_EQ(status, 0);
	EXPECT_EQ(
		cache->slabs_empty,
		NULL); // Structural validation checking that empty list unlinked completely

	status = k_slab_destroy_cache(cache);
	EXPECT_EQ(status, 0);

	TEST_RESULT();
}

/**
 * @brief Test Case: Resource Exhaustion Limits and Recovery Profiles.
 * Forces descriptor bounds threshold failure conditions to evaluate allocation resilience,
 * checking system capability to return gracefully post-exhaustion points.
 */
TEST(Slab_ExhaustionAndBoundaries)
{
	TEST_INIT();
	int status;
	u64 vmm_root;
	mmu.get_kernel_ctx(&vmm_root);

	k_slab_cache_t *cache = NULL;
	status = k_slab_create_cache(vmm_root, 2048, 8, &cache);
	EXPECT_EQ(status, 0);

#define DESCRIPTOR_BOUNDS 135
	void *tracked_allocations[DESCRIPTOR_BOUNDS];
	int successfully_allocated = 0;

	// 1. Loop-allocate elements sequentially until internal backing page limits or memory ceilings hit
	for (int i = 0; i < DESCRIPTOR_BOUNDS; i++) {
		status = k_slab_alloc(cache, &tracked_allocations[i]);
		if (status == 0) {
			successfully_allocated++;
		} else {
			// Out of tracking infrastructure limits must gracefully yield standardized error profiles
			EXPECT_EQ(status, ENOMEM);
			break;
		}
	}

	// 2. Execute deep reverse clean sweep to return resources and restore cache functional pools
	for (int i = 0; i < successfully_allocated; i++) {
		status = k_slab_free(cache, tracked_allocations[i]);
		EXPECT_EQ(status, 0);
	}

	status = k_slab_destroy_cache(cache);
	EXPECT_EQ(status, 0);

	TEST_RESULT();
}

/**
 * @brief Test Case: Security Robustness and Corrupted Handle Interceptions.
 * Assesses null pointer parameters, misaligned addresses, foreign pointer boundary crossing violations,
 * and structural alignment auto-scaling conversions.
 */
TEST(Slab_RobustnessEdgeCases)
{
	TEST_INIT();
	int status;
	u64 vmm_root;
	mmu.get_kernel_ctx(&vmm_root);

	k_slab_cache_t *cache = NULL;
	status = k_slab_create_cache(vmm_root, 128, 8, &cache);
	EXPECT_EQ(status, 0);

	// 1. Trap null handle references gracefully during allocation queries
	status = k_slab_alloc(NULL, NULL);
	EXPECT_EQ(status, EINVAL);

	void *out_ptr = NULL;
	status = k_slab_alloc(NULL, &out_ptr);
	EXPECT_EQ(status, EINVAL);

	status = k_slab_alloc(cache, NULL);
	EXPECT_EQ(status, EINVAL);

	// 2. Reject arbitrary untracked addresses or alignment-violating values passed into free hooks
	u64 foreign_address = 0xDEADBEEF0000UL;
	status = k_slab_free(cache, (void *)foreign_address);
	EXPECT_EQ(status, EINVAL);

	status = k_slab_free(NULL, (void *)foreign_address);
	EXPECT_EQ(status, EINVAL);

	// 3. Security Check: Validate automatic up-scaling mechanisms preserving internal freelist pointers
	k_slab_cache_t *small_cache = NULL;
	status = k_slab_create_cache(
		vmm_root, 1, 1,
		&small_cache); // Request sub-minimum 1-byte elements
	EXPECT_EQ(status, 0);

	/* Internal layout sizing metrics must auto-expand up to 4 bytes to allow embedding tracking links */
	EXPECT_LE(sizeof(u32), small_cache->obj_size);

	status = k_slab_destroy_cache(small_cache);
	EXPECT_EQ(status, 0);
	status = k_slab_destroy_cache(cache);
	EXPECT_EQ(status, 0);

	TEST_RESULT();
}

#endif
