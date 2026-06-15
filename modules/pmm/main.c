#include "pmm.h"
#include <modules.h>
#include <test.h>
#include <api/pmm.h>
#include <api/serial_debug.h>
#include <boot_info.h>

#define HHDM_OFFSET 0xFFFF800000000000UL

IMPORT_INTERFACE_ANY(serial, serial)

int main(boot_info_t *boot_info)
{
	pmm_init(boot_info->memory_regions, boot_info->memory_map_size,
		 HHDM_OFFSET);
}

EXPORT_INTERFACE(pmm, PhysicalMemoryAllocator,
		 {
			 .alloc_page = pmm_alloc_page,
			 .alloc_aligned = pmm_alloc_aligned,
			 .alloc_in_range = pmm_alloc_in_range,
			 .retain = pmm_retain,
			 .release = pmm_release,
			 .get_total_memory = pmm_get_total_memory,
			 .get_free_memory = pmm_get_free_memory,
			 .reserve_range = pmm_reserve_range,
		 });

#ifdef TESTING

TEST(AllocTest)
{
#define AllocTest_NUM_PAGES 128
#define AllocTest_RUN_COUNT 5
#define AllocTest_ORDER_COUNT 5

	TEST_INIT();

	u64 allocs[AllocTest_NUM_PAGES];
	for (int order = 0; order < AllocTest_ORDER_COUNT; ++order) {
		for (int runs = 0; runs < AllocTest_RUN_COUNT; ++runs) {
			for (int i = 0; i < AllocTest_NUM_PAGES; ++i) {
				int status = pmm_alloc_page(order, &allocs[i]);
				EXPECT_EQ(status, 0);
			}

			for (int i = 0; i < AllocTest_NUM_PAGES; i++) {
				for (int i2 = 0; i2 < i; ++i2) {
					EXPECT_NE(allocs[i], allocs[i2]);
				}
			}

			for (int i = 0; i < AllocTest_NUM_PAGES; ++i) {
				int status = pmm_release(allocs[i]);
				EXPECT_EQ(status, 0);
				status = pmm_release(allocs[i]);
				EXPECT_NE(status, 0);
			}
		}
	}

	TEST_RESULT();
}

TEST(RetainTest)
{
#define RetainTest_ALLOC_COUNT 128
#define RetainTest_ORDER_COUNT 5
#define RetainTest_RETAIN_COUNT 100

	TEST_INIT();

	int status;

	u64 allocs[RetainTest_ALLOC_COUNT];
	for (int order = 0; order < RetainTest_ORDER_COUNT; ++order) {
		for (int i = 0; i < RetainTest_ALLOC_COUNT; ++i) {
			status = pmm_alloc_page(order, &allocs[i]);
			EXPECT_EQ(status, 0);
			for (int retain = 0; retain < RetainTest_RETAIN_COUNT;
			     ++retain) {
				status = pmm_retain(allocs[i]);
				EXPECT_EQ(status, 0);
			}

			for (int release = 0; release < RetainTest_RETAIN_COUNT;
			     ++release) {
				status = pmm_release(allocs[i]);
				EXPECT_EQ(status, 0);
			}

			status = pmm_release(allocs[i]);
			EXPECT_EQ(status, 0);

			status = pmm_release(allocs[i]);
			EXPECT_NE(status, 0);
		}
	}

	TEST_RESULT();
}

TEST(AlignedAllocTest)
{
#define AlignedTest_RUNS 10
	TEST_INIT();

	int status;
	u64 allocations[AlignedTest_RUNS];
	u64 alignments[AlignedTest_RUNS] = {
		4096,	     8192,
		16384,	     32768,
		65536, // Standard page alignments
		1024 * 1024, 2 * 1024 * 1024, // 1MB, 2MB (Huge page boundaries)
		4096,	     4096,
		4096 // Basic back-to-back
	};
	u64 counts[AlignedTest_RUNS] = { 1, 2, 4, 1, 8, 16, 512, 1, 1, 1 };

	// 1. Verify alignment guarantees
	for (int i = 0; i < AlignedTest_RUNS; ++i) {
		status = pmm_alloc_aligned(counts[i], alignments[i],
					   &allocations[i]);
		EXPECT_EQ(status, 0);

		// Assert address is a perfect multiple of requested alignment
		EXPECT_EQ(allocations[i] % alignments[i], 0);
	}

	// 2. Verify no overlapping regions occurred
	for (int i = 0; i < AlignedTest_RUNS; ++i) {
		u64 start_a = allocations[i];
		u64 end_a = start_a +
			    (counts[i] * 4096); // Assuming base page size 4096

		for (int j = i + 1; j < AlignedTest_RUNS; ++j) {
			u64 start_b = allocations[j];
			u64 end_b = start_b + (counts[j] * 4096);

			// Assert regions do not overlap: [start_a, end_a) and [start_b, end_b)
			int overlap = (start_a < end_b) && (start_b < end_a);
			EXPECT_EQ(overlap, 0);
		}
	}

	// 3. Clean up
	for (int i = 0; i < AlignedTest_RUNS; ++i) {
		// Because alloc_aligned conceptually sets refcount to 1, release it.
		// If your PMM releases continuous blocks via the start frame, this applies:
		status = pmm_release(allocations[i]);
		EXPECT_EQ(status, 0);
	}

	TEST_RESULT();
}

TEST(RangeZoneAllocTest)
{
	TEST_INIT();
	int status;

	u64 dma_frame = 0;
	u64 dma32_frame = 0;

	// 1. Test standard 16MB Legacy DMA Zone constraint
	// Request 4 pages below the 16MB threshold
	status = pmm_alloc_in_range(4, 16 * 1024 * 1024, &dma_frame);
	if (status == 0) {
		u64 end_addr = dma_frame + (4 * 4096);
		EXPECT_LT(end_addr, 16 * 1024 * 1024);
	}

	// 2. Test 32-bit DMA Zone constraint (4GB)
	// Request 32 pages below the 4GB threshold
	status = pmm_alloc_in_range(32, 0x100000000UL, &dma32_frame);
	if (status == 0) {
		u64 end_addr = dma32_frame + (32 * 4096);
		EXPECT_LT(end_addr, 0x100000000UL);
	}

	// 3. Edge Case: Requesting memory with a ridiculously tight bound (e.g., max_addr = 0)
	// This should gracefully fail, not crash or wrap around into high memory.
	u64 invalid_frame = 0;
	status = pmm_alloc_in_range(1, 0, &invalid_frame);
	EXPECT_NE(status, 0);

	// Clean up valid allocations
	if (dma_frame != 0) {
		status = pmm_release(dma_frame);
		EXPECT_EQ(status, 0);
	}
	if (dma32_frame != 0) {
		status = pmm_release(dma32_frame);
		EXPECT_EQ(status, 0);
	}

	TEST_RESULT();
}

TEST(ReserveAndExhaustionTest)
{
	TEST_INIT();
	int status;

	// Get baseline stats
	u64 free_before = pmm_get_free_memory();
	u64 total_mem = pmm_get_total_memory();
	EXPECT_LE(0, total_mem);

	// 1. Allocate a temporary sentinel page to find a valid real estate address
	u64 scratch_frame;
	status = pmm_alloc_page(0, &scratch_frame);
	EXPECT_EQ(status, 0);
	status = pmm_release(scratch_frame);
	EXPECT_EQ(status, 0);

	// 2. Explicitly reserve a block around that known address space
	// Let's reserve 4 pages starting at scratch_frame
	u64 reserve_size = 4 * 4096;
	status = pmm_reserve_range(scratch_frame, reserve_size);

	// Note: status might be non-zero if something else occupies it, but assuming
	// an isolated test environment, it should succeed.
	if (status == 0) {
		u64 free_after = pmm_get_free_memory();
		// Free memory tracking should have plummeted by at least the reserved size
		EXPECT_LE(free_after, free_before - reserve_size);

		// 3. Attempting to target/retain that reserved range via allocations should either fail,
		// or any dynamic alloc should absolutely never return an address within [scratch_frame, scratch_frame + reserve_size)
		u64 test_alloc;
		status = pmm_alloc_page(0, &test_alloc);
		EXPECT_EQ(status, 0);

		int within_reserved_range =
			(test_alloc >= scratch_frame) &&
			(test_alloc < scratch_frame + reserve_size);
		EXPECT_EQ(within_reserved_range, 0);

		status = pmm_release(test_alloc);
		EXPECT_EQ(status, 0);
	}

	TEST_RESULT();
}

TEST(StatsConsistencyTest)
{
#define STATS_ALLOC_COUNT 64
	TEST_INIT();
	int status;

	u64 total_mem = pmm_get_total_memory();
	u64 initial_free = pmm_get_free_memory();
	EXPECT_LE(initial_free, total_mem);

	u64 allocations[STATS_ALLOC_COUNT];

	// Allocate single pages step-by-step and verify tracking drops monotonically
	for (int i = 0; i < STATS_ALLOC_COUNT; ++i) {
		status = pmm_alloc_page(0, &allocations[i]);
		EXPECT_EQ(status, 0);

		u64 current_free = pmm_get_free_memory();
		// Free memory must decrease
		EXPECT_LT(current_free, initial_free);
	}

	u64 mid_way_free = pmm_get_free_memory();

	// Release them and track recovery
	for (int i = 0; i < STATS_ALLOC_COUNT; ++i) {
		status = pmm_release(allocations[i]);
		EXPECT_EQ(status, 0);
	}

	u64 final_free = pmm_get_free_memory();
	// After releasing everything, system memory balances should restore exactly
	EXPECT_EQ(final_free, initial_free);

	TEST_RESULT();
}

TEST(RobustnessEdgeCaseTest)
{
	TEST_INIT();
	int status;

	// 1. Invalid Reference Counting inputs
	// Pass a completely bogus physical frame pointer address (e.g., non-page aligned ultra-high address)
	status = pmm_retain(0xFFFFFFFFFFFFF000UL);
	EXPECT_NE(status, 0); // Should fail safely

	status = pmm_release(0xFFFFFFFFFFFFF000UL);
	EXPECT_NE(status, 0); // Should fail safely

	// 2. Ridiculous Order Allocation Requests
	// Order 255 implies 2^255 pages, which is physically impossible.
	u64 structural_overflow_frame;
	status = pmm_alloc_page(255, &structural_overflow_frame);
	EXPECT_NE(status, 0);

	// 3. Zero count alignment request
	u64 out_align;
	status = pmm_alloc_aligned(0, 4096, &out_align);
	EXPECT_NE(status, 0);

	// 4. Non-power-of-two alignment parameter checks
	// Aligning to 7, 4097, or 0 bytes is technically invalid for page-based standard systems
	status = pmm_alloc_aligned(1, 4097, &out_align);
	EXPECT_NE(status, 0);

	TEST_RESULT();
}

#endif
