#include "pt.h"
#include "../modules.h"
#include "../test.h"
#include "../../include/api/mmu.h"
#include "../../include/api/pmm.h"
#include "../../include/api/serial_debug.h"

IMPORT_INTERFACE_ANY(pmm, pmm);
IMPORT_INTERFACE_ANY(serial, serial)

EXPORT_INTERFACE(mmu, MemoryManagementUnit,
		 {
			 .alloc = pt_alloc,
			 .free = pt_free,
			 .copy = pt_copy,
			 .set_user_ctx = pt_set_user_ctx,
			 .set_kernel_ctx = pt_set_kernel_ctx,
			 .map = pt_map,
			 .unmap = pt_unmap,
			 .protect = pt_protect,
			 .translate = pt_translate,
			 .flush = pt_flush,
			 .invalidate = pt_invalidate,
			 .set_mair = pt_set_mair,
		 });

TEST(PageTableCreation)
{
#define PageTableCreation_TEST_COUNT 5
#define PageTableCreation_TEST_RANGE 128

	TEST_INIT();

	int status;
	for (int count = 0; count < PageTableCreation_TEST_COUNT; ++count) {
		u64 root;
		status = pt_alloc(&root);
		EXPECT_NE(root, 0);
		EXPECT_EQ(status, 0);

		for (u64 virt = 0; virt < PageTableCreation_TEST_RANGE;
		     ++virt) {
			status = pt_map(root, virt * 4096 + 4096, virt, 1,
					PS_4KB, 0);
			EXPECT_NE(root, 0);
		}

		status = pt_free(root);
		EXPECT_EQ(status, 0);
	}

	TEST_RESULT();
}
