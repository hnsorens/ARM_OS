#include "pt.h"
#include <modules.h>
#include <test.h>
#include <api/mmu.h>
#include <api/pmm.h>
#include <api/serial_debug.h>

IMPORT_INTERFACE_ANY(pmm, pmm);
IMPORT_INTERFACE_ANY(serial, serial)

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

	u64 virt_addr =
		0x00007FFFF0000000UL; // Standard high user-space address
	u64 phys_addr = 0x10000000UL; // Target physical address
	enum mmu_flags map_flags = MMU_USER;

	// 1. Map a single 4KB page
	status = pt_map(root, virt_addr, phys_addr, 1, PS_4KB, map_flags);
	EXPECT_EQ(status, 0);

	// 2. Translate it back and verify values match perfectly
	u64 out_phys = 0;
	enum mmu_flags out_flags = 0;
	status = pt_translate(root, virt_addr, &out_phys, &out_flags);

	EXPECT_EQ(status, 0);
	EXPECT_EQ(out_phys, phys_addr);
	EXPECT_EQ(out_flags, map_flags);

	// 3. Ensure a non-mapped address safely fails translation instead of returning junk
	u64 unmapped_phys;
	enum mmu_flags unmapped_flags;
	status = pt_translate(root, virt_addr + 0x5000, &unmapped_phys,
			      &unmapped_flags);
	EXPECT_NE(status, 0); // Translation should fail

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

	// 1. Test 2MB Page Mapping (Must be 2MB aligned)
	u64 virt_2mb = 0x00007FFF00000000UL;
	u64 phys_2mb = 0x40000000UL;
	status = pt_map(root, virt_2mb, phys_2mb, 1, PS_2MB, MMU_USER);
	EXPECT_EQ(status, 0);

	u64 out_phys;
	enum mmu_flags out_flags;
	status = pt_translate(root, virt_2mb, &out_phys, &out_flags);
	EXPECT_EQ(status, 0);
	EXPECT_EQ(out_phys, phys_2mb);

	// 2. Test 1GB Page Mapping (Must be 1GB aligned)
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

	// Start with a highly permissive Read/Write page
	status = pt_map(root, virt, phys, 1, PS_4KB, MMU_USER);
	EXPECT_EQ(status, 0);

	// Protect: strip MMU_WRITE permission, leave it Read-Only
	status = pt_protect(root, virt, 1, PS_4KB, MMU_RO | MMU_USER);
	EXPECT_EQ(status, 0);

	// Read back to confirm protection took effect
	u64 out_phys;
	enum mmu_flags out_flags;
	status = pt_translate(root, virt, &out_phys, &out_flags);
	EXPECT_EQ(status, 0);
	EXPECT_EQ(out_phys, phys);

	// Check that write bit is gone, but read bit remains
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

	// 1. Establish original source mapping
	status = pt_map(src_root, virt, phys, 1, PS_4KB, MMU_USER);
	EXPECT_EQ(status, 0);

	// 2. Clone the directory root layout completely
	status = pt_copy(src_root, &dest_root);
	EXPECT_EQ(status, 0);
	EXPECT_NE(src_root, dest_root); // Must be different root entries

	// 3. Verify destination has cloned the matching properties
	u64 out_phys;
	enum mmu_flags out_flags;
	status = pt_translate(dest_root, virt, &out_phys, &out_flags);
	EXPECT_EQ(status, 0);
	EXPECT_EQ(out_phys, phys);

	// 4. Verify deep isolation: Unmapping from source shouldn't damage destination
	status = pt_unmap(src_root, virt, 1, PS_4KB);
	EXPECT_EQ(status, 0);

	// Target translation must still stand securely in destination branch
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

	// TODO finish test later
	TEST_SKIP();

	int status;
	u64 root_proc1;
	u64 root_proc2;

	status = pt_alloc(&root_proc1);
	EXPECT_EQ(status, 0);
	status = pt_alloc(&root_proc2);
	EXPECT_EQ(status, 0);

	// Switch active user address space context to Process 1 (ACID 1)
	status = pt_set_user_ctx(root_proc1, 1);
	EXPECT_EQ(status, 0);

	// Invalidate 4 pages at a specific address to sweep away stale TLB traces
	status = pt_invalidate(0x00007FFFF0000000UL, 4, PS_4KB);
	EXPECT_EQ(status, 0);

	// Switch context to Process 2 (ACID 2)
	status = pt_set_user_ctx(root_proc2, 2);
	EXPECT_EQ(status, 0);

	// Wipe global context caches completely
	status = pt_flush();
	EXPECT_EQ(status, 0);

	status = pt_free(root_proc1);
	EXPECT_EQ(status, 0);
	status = pt_free(root_proc2);
	EXPECT_EQ(status, 0);

	TEST_RESULT();
}

#endif
