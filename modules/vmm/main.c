#include "vmm.h"
#include "../modules.h"

#include "../../include/api/vmm.h"
#include "../../include/api/pmm.h"
#include "../../include/api/mmu.h"
#include "../../include/api/serial_debug.h"
#include "../../include/errno.h"
#include "../test.h"

EXPORT_INTERFACE(vmm, VirtualMemoryManager,
		 { .space_create = vmm_space_create,
		   .space_destroy = vmm_space_destroy,
		   .allocate = vmm_allocate,
		   .reserve = vmm_reserve,
		   .free = vmm_free,
		   .resize = vmm_resize,
		   .map_external = vmm_map_external,
		   .protect = vmm_protect,
		   .query = vmm_query,
		   .activate = vmm_activate,
		   .sync = vmm_sync });

IMPORT_INTERFACE_ANY(pmm, pmm);
IMPORT_INTERFACE_ANY(mmu, mmu);
IMPORT_INTERFACE_ANY(serial, serial);

TEST(VMM_SpaceLifecycle)
{
	TEST_INIT();
	int status;
	u64 root = 0;

	// 1. Creation and basic destruction
	status = vmm_space_create(&root);
	EXPECT_EQ(status, 0);
	EXPECT_NE(root, 0);

	status = vmm_space_destroy(root);
	EXPECT_EQ(status, 0);

	// 2. Reject bad inputs
	status = vmm_space_create(NULL);
	EXPECT_EQ(status, EINVAL);

	status = vmm_space_destroy(0);
	EXPECT_EQ(status, EINVAL);

	TEST_RESULT();
}

TEST(VMM_AllocationAndQuery)
{
	TEST_INIT();
	int status;
	u64 root;

	status = vmm_space_create(&root);
	EXPECT_EQ(status, 0);

	// 1. Allocate a standard chunk of anonymous dynamic memory
	u64 vaddr = 0x00007FFFF0000000UL;
	u64 requested_sz = 4096 * 3; // 3 pages
	status = vmm_allocate(root, &vaddr, requested_sz, MMU_USER,
			      VMM_REGION_DATA);
	EXPECT_EQ(status, 0);
	EXPECT_NE(vaddr, 0);

	// 2. Query the exact midsection of our new allocation
	struct vmm_region_info info;
	status = vmm_query(root, vaddr + 4096, &info);
	EXPECT_EQ(status, 0);
	EXPECT_EQ(info.base, vaddr);
	EXPECT_EQ(info.size, 4096 * 3);
	EXPECT_EQ(info.type, VMM_REGION_DATA);
	EXPECT_EQ(info.is_paged, true);

	// 3. Ensure querying an unallocated address safely fails
	status = vmm_query(root, vaddr + (4096 * 10), &info);
	EXPECT_NE(status, 0);

	status = vmm_space_destroy(root);
	EXPECT_EQ(status, 0);
	TEST_RESULT();
}

TEST(VMM_StackGuardLifecycle)
{
	TEST_INIT();
	int status;
	u64 root;

	status = vmm_space_create(&root);
	EXPECT_EQ(status, 0);

	// 1. Allocate a downward growing stack frame region
	u64 stack_vaddr = 0x00007FFFF0000000UL;
	u64 stack_sz = 4096 * 4;
	status = vmm_allocate(root, &stack_vaddr, stack_sz, MMU_USER,
			      VMM_REGION_STACK);
	EXPECT_EQ(status, 0);

	// 2. Verify that a hidden guard area was added immediately before the stack base
	struct vmm_region_info guard_info;
	status = vmm_query(root, stack_vaddr - 4096, &guard_info);
	EXPECT_EQ(status, 0);
	EXPECT_EQ(guard_info.type, VMM_REGION_GUARD);
	EXPECT_EQ(guard_info.size, 4096);
	EXPECT_EQ(guard_info.is_paged, false);

	// 3. Free the stack frame and ensure the VMM cleans up the guard page automatically
	status = vmm_free(root, stack_vaddr, stack_sz);
	EXPECT_EQ(status, 0);

	status = vmm_query(root, stack_vaddr - 4096, &guard_info);
	EXPECT_NE(status, 0); // The guard tracking structure should be gone

	status = vmm_space_destroy(root);
	EXPECT_EQ(status, 0);
	TEST_RESULT();
}

TEST(VMM_FreeSubrangeSplitting)
{
	TEST_INIT();
	int status;
	u64 root;

	status = vmm_space_create(&root);
	EXPECT_EQ(status, 0);

	// 1. Allocate a contiguous block spanning 5 pages
	u64 vaddr = 0x00007FFFF0000000UL;
	u64 total_sz = 4096 * 5;
	status =
		vmm_allocate(root, &vaddr, total_sz, MMU_USER, VMM_REGION_DATA);
	EXPECT_EQ(status, 0);

	// 2. Punch a hole right through the center (Free page indexing index 2)
	u64 punch_vaddr = vaddr + (4096 * 2);
	status = vmm_free(root, punch_vaddr, 4096);
	EXPECT_EQ(status, 0);

	// 3. Verify the layout was fractured into two distinct valid tracking sections
	struct vmm_region_info left_chunk;
	status = vmm_query(root, vaddr, &left_chunk);
	EXPECT_EQ(status, 0);
	EXPECT_EQ(left_chunk.size, 4096 * 2);

	struct vmm_region_info right_chunk;
	status = vmm_query(root, vaddr + (4096 * 3), &right_chunk);
	EXPECT_EQ(status, 0);
	EXPECT_EQ(right_chunk.base, vaddr + (4096 * 3));
	EXPECT_EQ(right_chunk.size, 4096 * 2);

	// 4. Ensure the middle hole returns unmapped/invalid on queries
	struct vmm_region_info middle_hole;
	status = vmm_query(root, punch_vaddr, &middle_hole);
	EXPECT_NE(status, 0);

	status = vmm_space_destroy(root);
	EXPECT_EQ(status, 0);
	TEST_RESULT();
}

TEST(VMM_DynamicResizing)
{
	TEST_INIT();
	int status;
	u64 root;

	status = vmm_space_create(&root);
	EXPECT_EQ(status, 0);

	u64 vaddr = 0x00007FFFF0000000UL;
	status =
		vmm_allocate(root, &vaddr, 4096 * 2, MMU_USER, VMM_REGION_DATA);
	EXPECT_EQ(status, 0);

	// 1. Expand the allocation outward safely
	status = vmm_resize(root, vaddr, 4096 * 2, 4096 * 5);
	EXPECT_EQ(status, 0);

	struct vmm_region_info expand_info;
	status = vmm_query(root, vaddr, &expand_info);
	EXPECT_EQ(status, 0);
	EXPECT_EQ(expand_info.size, 4096 * 5);

	// 2. Shrink the tracking footprint backward
	status = vmm_resize(root, vaddr, 4096 * 5, 4096 * 1);
	EXPECT_EQ(status, 0);

	struct vmm_region_info shrink_info;
	status = vmm_query(root, vaddr, &shrink_info);
	EXPECT_EQ(status, 0);
	EXPECT_EQ(shrink_info.size, 4096 * 1);

	// 3. Test expansion collision (Allocate a blocker directly adjacent)
	u64 blocker_vaddr = vaddr + 4096;
	status = vmm_allocate(root, &blocker_vaddr, 4096, MMU_USER,
			      VMM_REGION_DATA);
	EXPECT_EQ(status, 0);

	// Attempting expansion into the occupied address range must fail with ENOMEM
	status = vmm_resize(root, vaddr, 4096 * 1, 4096 * 3);
	EXPECT_EQ(status, ENOMEM);

	status = vmm_space_destroy(root);
	EXPECT_EQ(status, 0);
	TEST_RESULT();
}

TEST(VMM_ProtectFragmentation)
{
	TEST_INIT();
	int status;
	u64 root;

	status = vmm_space_create(&root);
	EXPECT_EQ(status, 0);

	// 1. Establish initial uniform permissions block
	u64 vaddr = 0x00007FFFF0000000UL;
	u64 total_sz = 4096 * 3;
	status =
		vmm_allocate(root, &vaddr, total_sz, MMU_USER, VMM_REGION_DATA);
	EXPECT_EQ(status, 0);

	// 2. Modify permissions of the center page only (Triggering a 3-way tracking split)
	u64 mid_vaddr = vaddr + 4096;
	status = vmm_protect(root, mid_vaddr, 4096, MMU_RO | MMU_USER);
	EXPECT_EQ(status, 0);

	// 3. Verify fragmentation consistency across left, middle, and right regions
	struct vmm_region_info left, mid, right;

	status = vmm_query(root, vaddr, &left);
	EXPECT_EQ(status, 0);
	EXPECT_EQ(left.size, 4096);
	EXPECT_EQ(left.flags, MMU_USER);

	status = vmm_query(root, mid_vaddr, &mid);
	EXPECT_EQ(status, 0);
	EXPECT_EQ(mid.size, 4096);
	EXPECT_EQ(mid.flags, MMU_RO | MMU_USER);

	status = vmm_query(root, vaddr + (4096 * 2), &right);
	EXPECT_EQ(status, 0);
	EXPECT_EQ(right.size, 4096);
	EXPECT_EQ(right.flags, MMU_USER);

	status = vmm_space_destroy(root);
	EXPECT_EQ(status, 0);
	TEST_RESULT();
}

TEST(VMM_ExhaustionLimits)
{
#define MAX_VMA_POOL_SIZE 512
	TEST_INIT();
	int status;
	u64 root;

	status = vmm_space_create(&root);
	EXPECT_EQ(status, 0);

	// Exhaust static allocation capacity pools completely
	u64 allocated_addresses[MAX_VMA_POOL_SIZE + 5];
	int allocation_count = 0;

	for (int i = 0; i < MAX_VMA_POOL_SIZE + 2; i++) {
		u64 hint = 0; // Let find_unmapped_area scan dynamically
		status = vmm_allocate(root, &hint, 4096, MMU_USER,
				      VMM_REGION_DATA);

		if (status == 0) {
			allocated_addresses[allocation_count++] = hint;
		} else {
			// Once structural memory capacity triggers failure, verify it reports correctly
			EXPECT_EQ(status, ENOMEM);
			break;
		}
	}

	// Ensure we can clear everything back down to an empty baseline pool state safely
	for (int i = 0; i < allocation_count; i++) {
		status = vmm_free(root, allocated_addresses[i], 4096);
		EXPECT_EQ(status, 0);
	}

	status = vmm_space_destroy(root);
	EXPECT_EQ(status, 0);
	TEST_RESULT();
}
