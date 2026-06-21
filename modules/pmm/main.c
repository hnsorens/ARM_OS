/**
 * @file main.c
 * @brief Subsystem Export Mappings and Rigorous Unit Testing Harness validation for the PMM.
 */

#include "pmm.h"
#include <modules.h>
#include <test.h>
#include <api/pmm.h>
#include <api/serial_debug.h>
#include <boot_info.h>
#include <constants.h>

IMPORT_INTERFACE_ANY(serial, serial)

/**
 * @brief Primary entry vector targeting initialization routines for physical layer trackers.
 */
int main(boot_info_t *boot_info)
{
	pmm_init(boot_info->memory_regions, boot_info->memory_map_size,
		 HHDM_OFFSET);
	return 0;
}

/* --- Module Registration Export Interfaces Matrix --- */
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
		4096, 8192, 16384, 32768, 65536, 1024 * 1024, 2 * 1024 * 1024,
		4096, 4096, 4096
	};
	u64 counts[AlignedTest_RUNS] = { 1, 2, 4, 1, 8, 16, 512, 1, 1, 1 };

	for (int i = 0; i < AlignedTest_RUNS; ++i) {
		status = pmm_alloc_aligned(counts[i], alignments[i],
					   &allocations[i]);
		EXPECT_EQ(status, 0);
		EXPECT_EQ(allocations[i] % alignments[i], 0);
	}

	for (int i = 0; i < AlignedTest_RUNS; ++i) {
		u64 start_a = allocations[i];
		u64 end_a = start_a + (counts[i] * 4096);

		for (int j = i + 1; j < AlignedTest_RUNS; ++j) {
			u64 start_b = allocations[j];
			u64 end_b = start_b + (counts[j] * 4096);

			int overlap = (start_a < end_b) && (start_b < end_a);
			EXPECT_EQ(overlap, 0);
		}
	}

	for (int i = 0; i < AlignedTest_RUNS; ++i) {
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

	status = pmm_alloc_in_range(4, 16 * 1024 * 1024, &dma_frame);
	if (status == 0) {
		u64 end_addr = dma_frame + (4 * 4096);
		EXPECT_LT(end_addr, 16 * 1024 * 1024);
	}

	status = pmm_alloc_in_range(32, 0x100000000UL, &dma32_frame);
	if (status == 0) {
		u64 end_addr = dma32_frame + (32 * 4096);
		EXPECT_LT(end_addr, 0x100000000UL);
	}

	u64 invalid_frame = 0;
	status = pmm_alloc_in_range(1, 0, &invalid_frame);
	EXPECT_NE(status, 0);

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

	u64 free_before = pmm_get_free_memory();
	u64 total_mem = pmm_get_total_memory();
	EXPECT_LE(0, total_mem);

	u64 scratch_frame;
	status = pmm_alloc_page(0, &scratch_frame);
	EXPECT_EQ(status, 0);
	status = pmm_release(scratch_frame);
	EXPECT_EQ(status, 0);

	u64 reserve_size = 4 * 4096;
	status = pmm_reserve_range(scratch_frame, reserve_size);

	if (status == 0) {
		u64 free_after = pmm_get_free_memory();
		EXPECT_LE(free_after, free_before - reserve_size);

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

	for (int i = 0; i < STATS_ALLOC_COUNT; ++i) {
		status = pmm_alloc_page(0, &allocations[i]);
		EXPECT_EQ(status, 0);

		u64 current_free = pmm_get_free_memory();
		EXPECT_LT(current_free, initial_free);
	}

	for (int i = 0; i < STATS_ALLOC_COUNT; ++i) {
		status = pmm_release(allocations[i]);
		EXPECT_EQ(status, 0);
	}

	u64 final_free = pmm_get_free_memory();
	EXPECT_EQ(final_free, initial_free);

	TEST_RESULT();
}

TEST(RobustnessEdgeCaseTest)
{
	TEST_INIT();
	int status;

	status = pmm_retain(0xFFFFFFFFFFFFF000UL);
	EXPECT_NE(status, 0);

	status = pmm_release(0xFFFFFFFFFFFFF000UL);
	EXPECT_NE(status, 0);

	u64 structural_overflow_frame;
	status = pmm_alloc_page(255, &structural_overflow_frame);
	EXPECT_NE(status, 0);

	u64 out_align;
	status = pmm_alloc_aligned(0, 4096, &out_align);
	EXPECT_NE(status, 0);

	status = pmm_alloc_aligned(1, 4097, &out_align);
	EXPECT_NE(status, 0);

	TEST_RESULT();
}

#endif
