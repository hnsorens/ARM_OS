#include "../modules.h"

#include "../test.h"

#include "../../include/api/pmm.h"
#include "../../include/api/serial_debug.h"
#include "../boot_info.h"

#include "pmm.h"

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
