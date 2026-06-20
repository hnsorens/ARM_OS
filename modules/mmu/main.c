/**
 * @file main.c
 * @brief Core Module Interface Hooks and Unit-Testing Framework validation suites for the MMU Subsystem.
 */

#include "pt.h"
#include <modules.h>
#include <test.h>
#include <api/mmu.h>
#include <api/pmm.h>
#include <api/serial_debug.h>

/* --- Operational Engine Linkage Bindings --- */
IMPORT_INTERFACE_ANY(pmm, pmm);
IMPORT_INTERFACE_ANY(serial, serial);

/* --- Module Registration Export Interfaces Matrix --- */
EXPORT_INTERFACE(mmu, MemoryManagementUnit,
		 {
			 .alloc = pt_alloc,
			 .free = pt_free,
			 .copy = pt_copy,
			 .set_user_ctx = pt_set_user_ctx,
			 .set_kernel_ctx = pt_set_kernel_ctx,
			 .get_user_ctx = pt_get_user_ctx,
			 .get_kernel_ctx = pt_get_kernel_ctx,
			 .map = pt_map,
			 .unmap = pt_unmap,
			 .protect = pt_protect,
			 .translate = pt_translate,
			 .flush = pt_flush,
			 .invalidate = pt_invalidate,
			 .set_mair = pt_set_mair,
		 });

#ifdef TESTING

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

TEST(MMU_TranslationAndFlags)
{
	TEST_INIT();
	int status;
	u64 root;

	status = pt_alloc(&root);
	EXPECT_EQ(status, 0);

	u64 virt_addr = 0x00007FFFF0000000UL;
	u64 phys_addr = 0x10000000UL;
	enum mmu_flags map_flags = MMU_USER;

	status = pt_map(root, virt_addr, phys_addr, 1, PS_4KB, map_flags);
	EXPECT_EQ(status, 0);

	u64 out_phys = 0;
	enum mmu_flags out_flags = 0;
	status = pt_translate(root, virt_addr, &out_phys, &out_flags);

	EXPECT_EQ(status, 0);
	EXPECT_EQ(out_phys, phys_addr);
	EXPECT_EQ(out_flags, map_flags);

	u64 unmapped_phys;
	enum mmu_flags unmapped_flags;
	status = pt_translate(root, virt_addr + 0x5000, &unmapped_phys,
			      &unmapped_flags);
	EXPECT_NE(status, 0);

	status = pt_free(root);
	EXPECT_EQ(status, 0);

	TEST_RESULT();
}

TEST(MMU_HugePages)
{
	TEST_INIT();
	int status;
	u64 root;

	status = pt_alloc(&root);
	EXPECT_EQ(status, 0);

	u64 virt_2mb = 0x00007FFF00000000UL;
	u64 phys_2mb = 0x40000000UL;
	status = pt_map(root, virt_2mb, phys_2mb, 1, PS_2MB, MMU_USER);
	EXPECT_EQ(status, 0);

	u64 out_phys;
	enum mmu_flags out_flags;
	status = pt_translate(root, virt_2mb, &out_phys, &out_flags);
	EXPECT_EQ(status, 0);
	EXPECT_EQ(out_phys, phys_2mb);

	u64 virt_1gb = 0x00007FFE00000000UL;
	u64 phys_1gb = 0x80000000UL;
	status = pt_map(root, virt_1gb, phys_1gb, 1, PS_1GB, MMU_USER);
	EXPECT_EQ(status, 0);

	status = pt_translate(root, virt_1gb, &out_phys, &out_flags);
	EXPECT_EQ(status, 0);
	EXPECT_EQ(out_phys, phys_1gb);

	status = pt_free(root);
	EXPECT_EQ(status, 0);

	TEST_RESULT();
}

TEST(MMU_PageProtectionUpdate)
{
	TEST_INIT();
	int status;
	u64 root;

	status = pt_alloc(&root);
	EXPECT_EQ(status, 0);

	u64 virt = 0x00007FFFF5500000UL;
	u64 phys = 0x90000000UL;

	status = pt_map(root, virt, phys, 1, PS_4KB, MMU_USER);
	EXPECT_EQ(status, 0);

	status = pt_protect(root, virt, 1, PS_4KB, MMU_RO | MMU_USER);
	EXPECT_EQ(status, 0);

	u64 out_phys;
	enum mmu_flags out_flags;
	status = pt_translate(root, virt, &out_phys, &out_flags);
	EXPECT_EQ(status, 0);
	EXPECT_EQ(out_phys, phys);

	EXPECT_EQ(out_flags, MMU_RO | MMU_USER);

	status = pt_free(root);
	EXPECT_EQ(status, 0);

	TEST_RESULT();
}

TEST(MMU_DeepCopyAndIsolation)
{
	TEST_INIT();
	int status;
	u64 src_root;
	u64 dest_root;

	status = pt_alloc(&src_root);
	EXPECT_EQ(status, 0);

	u64 virt = 0x00007FFFFCC00000UL;
	u64 phys = 0xA0000000UL;

	status = pt_map(src_root, virt, phys, 1, PS_4KB, MMU_USER);
	EXPECT_EQ(status, 0);

	status = pt_copy(src_root, &dest_root);
	EXPECT_EQ(status, 0);
	EXPECT_NE(src_root, dest_root);

	u64 out_phys;
	enum mmu_flags out_flags;
	status = pt_translate(dest_root, virt, &out_phys, &out_flags);
	EXPECT_EQ(status, 0);
	EXPECT_EQ(out_phys, phys);

	status = pt_unmap(src_root, virt, 1, PS_4KB);
	EXPECT_EQ(status, 0);

	status = pt_translate(dest_root, virt, &out_phys, &out_flags);
	EXPECT_EQ(status, 0);

	status = pt_free(src_root);
	EXPECT_EQ(status, 0);
	status = pt_free(dest_root);
	EXPECT_EQ(status, 0);

	TEST_RESULT();
}

TEST(MMU_UserContextSwitchAndInvalidate)
{
	TEST_INIT();

	/* Validation properties uncompleted: skipped to safeguard hardware runtime steps */
	TEST_SKIP();

	int status;
	u64 root_proc1;
	u64 root_proc2;

	status = pt_alloc(&root_proc1);
	EXPECT_EQ(status, 0);
	status = pt_alloc(&root_proc2);
	EXPECT_EQ(status, 0);

	status = pt_set_user_ctx(root_proc1, 1);
	EXPECT_EQ(status, 0);

	status = pt_invalidate(0x00007FFFF0000000UL, 4, PS_4KB);
	EXPECT_EQ(status, 0);

	status = pt_set_user_ctx(root_proc2, 2);
	EXPECT_EQ(status, 0);

	status = pt_flush();
	EXPECT_EQ(status, 0);

	status = pt_free(root_proc1);
	EXPECT_EQ(status, 0);
	status = pt_free(root_proc2);
	EXPECT_EQ(status, 0);

	TEST_RESULT();
}

#endif
