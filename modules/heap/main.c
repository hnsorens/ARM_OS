#include "heap.h"
#include "../modules.h"
#include "../test.h"
#include "../../include/api/heap.h"
#include "../../include/api/vmm.h"
#include "../../include/api/serial_debug.h"
#include "../../include/errno.h"

IMPORT_INTERFACE_ANY(vmm, vmm);
IMPORT_INTERFACE_ANY(serial, serial);

// for test
IMPORT_INTERFACE_ANY(mmu, mmu);

EXPORT_INTERFACE(heap, Heap,
		 { .malloc = heap_malloc,
		   .free = heap_free,
		   .realloc = heap_realloc,
		   .memalign = heap_memalign,
		   .get_stats = heap_get_stats });

TEST(Heap_LifecycleAndBasicMalloc)
{
	TEST_INIT();

	struct heap_context *heap = NULL;
	u64 vmm_root;
	mmu.get_kernel_ctx(&vmm_root);
	u64 initial_size = 64 * 1024; // 64 KB
	int status;

	// 1. Initialize a dynamic self-bootstrapped heap instance
	status = heap_create(vmm_root, initial_size, &heap);
	EXPECT_EQ(status, 0);
	EXPECT_NE(heap, NULL);

	// 2. Validate clean tracking parameters
	u64 used = 0, total = 0;
	status = heap_get_stats(heap, &used, &total);
	EXPECT_EQ(status, 0);
	EXPECT_EQ(used, 0);
	EXPECT_LT(initial_size, total);

	// 3. Allocate a simple sequential buffer
	void *ptr1 = NULL;
	status = heap_malloc(heap, 256, &ptr1);
	EXPECT_EQ(status, 0);
	EXPECT_NE(ptr1, NULL);

	// 4. Ensure payload metrics adjusted correctly
	status = heap_get_stats(heap, &used, &total);
	EXPECT_LE(256, used); // Payload size + boundary tag headers

	// 5. Release block back safely
	status = heap_free(heap, ptr1);
	EXPECT_EQ(status, 0);

	// 6. Ensure statistics settle back down perfectly
	status = heap_get_stats(heap, &used, &total);
	EXPECT_EQ(used, 0);

	// 7. Tear down the instance context
	status = heap_destroy(heap);
	EXPECT_EQ(status, 0);

	TEST_RESULT();
}

TEST(Heap_FragmentationAndCoalescing)
{
	TEST_INIT();

	struct heap_context *heap = NULL;
	u64 vmm_root;
	mmu.get_kernel_ctx(&vmm_root);
	int status = heap_create(vmm_root, 128 * 1024, &heap);
	EXPECT_EQ(status, 0);

	void *p1 = NULL, *p2 = NULL, *p3 = NULL;

	// Allocate three contiguous tracking entries
	status = heap_malloc(heap, 1024, &p1);
	EXPECT_EQ(status, 0);
	status = heap_malloc(heap, 1024, &p2);
	EXPECT_EQ(status, 0);
	status = heap_malloc(heap, 1024, &p3);
	EXPECT_EQ(status, 0);

	u64 baseline_used = 0;
	heap_get_stats(heap, &baseline_used, NULL);

	// Free alternating slots to isolate p2
	status = heap_free(heap, p1);
	EXPECT_EQ(status, 0);
	status = heap_free(heap, p3);
	EXPECT_EQ(status, 0);

	// Freeing middle block p2 should trigger bidirectional contiguous layout coalescing
	status = heap_free(heap, p2);
	EXPECT_EQ(status, 0);

	u64 final_used = 1;
	heap_get_stats(heap, &final_used, NULL);
	EXPECT_EQ(
		final_used,
		0); // Everything must cleanly merge backward and forward to zero

	heap_destroy(heap);
	TEST_RESULT();
}

TEST(Heap_ReallocInPlaceAndMigration)
{
	TEST_INIT();

	struct heap_context *heap = NULL;
	u64 vmm_root;
	mmu.get_kernel_ctx(&vmm_root);
	int status = heap_create(vmm_root, 128 * 1024, &heap);
	heap_create(vmm_root, 256 * 1024, &heap);

	void *p1 = NULL;
	status = heap_malloc(heap, 128, &p1);
	EXPECT_EQ(status, 0);

	// Fill buffer with signature verification values
	u8 *data = (u8 *)p1;
	for (int i = 0; i < 128; i++) {
		data[i] = (u8)i;
	}

	// 1. Shrink or maintain matching boundaries (Should maintain structural pointer layout in-place)
	void *p2 = NULL;
	status = heap_realloc(heap, p1, 64, &p2);
	EXPECT_EQ(status, 0);
	EXPECT_EQ(p1, p2);

	// Verify data integrity post-shrink
	data = (u8 *)p2;
	for (int i = 0; i < 64; i++) {
		EXPECT_EQ(data[i], (u8)i);
	}

	// 2. Allocate a trapping barrier to prevent forward in-place scaling expansion optimization passes
	void *barrier = NULL;
	heap_malloc(heap, 256, &barrier);

	// 3. Force migration pass by requesting size that exceeds existing tracking limits
	void *p3 = NULL;
	status = heap_realloc(heap, p2, 2048, &p3);
	EXPECT_EQ(status, 0);
	EXPECT_NE(
		p2,
		p3); // Must migrate because barrier blocked linear forward expansion

	// Ensure data payload safely mirrored tracking migration paths
	data = (u8 *)p3;
	for (int i = 0; i < 64; i++) {
		EXPECT_EQ(data[i], (u8)i);
	}

	heap_free(heap, barrier);
	heap_free(heap, p3);
	heap_destroy(heap);
	TEST_RESULT();
}

TEST(Heap_MemalignBoundaryChecks)
{
	TEST_INIT();

	struct heap_context *heap = NULL;
	u64 vmm_root;
	mmu.get_kernel_ctx(&vmm_root);
	int status = heap_create(vmm_root, 128 * 1024, &heap);
	heap_create(vmm_root, 512 * 1024, &heap);

	void *aligned_ptr = NULL;

	// 1. Test 64-byte alignment constraint
	status = heap_memalign(heap, 64, 1023, &aligned_ptr);
	EXPECT_EQ(status, 0);
	EXPECT_NE(aligned_ptr, NULL);
	EXPECT_EQ(((uintptr_t)aligned_ptr & (64 - 1)), 0);

	// 2. Test ultra-strict cache line page alignment constraints (e.g. 4096 bytes)
	void *page_aligned_ptr = NULL;
	status = heap_memalign(heap, 4096, 8000, &page_aligned_ptr);
	EXPECT_EQ(status, 0);
	EXPECT_NE(page_aligned_ptr, NULL);
	EXPECT_EQ(((uintptr_t)page_aligned_ptr & (4096 - 1)), 0);

	// 3. Verify standard functionality path release parameters handle padding-split items correctly
	status = heap_free(heap, aligned_ptr);
	EXPECT_EQ(status, 0);
	status = heap_free(heap, page_aligned_ptr);
	EXPECT_EQ(status, 0);

	heap_destroy(heap);
	TEST_RESULT();
}

TEST(Heap_SecurityAndEdgeCases)
{
	TEST_INIT();

	struct heap_context *heap = NULL;
	u64 vmm_root;
	mmu.get_kernel_ctx(&vmm_root);
	int status = heap_create(vmm_root, 128 * 1024, &heap);
	heap_create(vmm_root, 64 * 1024, &heap);

	void *out_ptr = (void *)0xDEADBEEF;

	// 1. Zero size allocation requests should safely set pointers to NULL without generating errors
	status = heap_malloc(heap, 0, &out_ptr);
	EXPECT_EQ(status, 0);
	EXPECT_EQ(out_ptr, NULL);

	// 2. Freeing a standard NULL target address should gracefully pass as a structural no-op
	status = heap_free(heap, NULL);
	EXPECT_EQ(status, 0);

	// 3. Passing non-power-of-two values down to alignment constraints should trigger validation faults
	status = heap_memalign(heap, 31, 128, &out_ptr);
	EXPECT_EQ(status, EINVAL);

	// 4. Overallocation out-of-memory error bounds protection checks
	status = heap_malloc(heap, 1024 * 1024, &out_ptr);
	EXPECT_EQ(status, ENOMEM);

	// 5. Basic anti-corruption validation check using modified boundary headers
	void *corrupted_ptr = NULL;
	heap_malloc(heap, 64, &corrupted_ptr);

	// Intentionally break the boundary magic number tracking token field context
	struct heap_block *block =
		(struct heap_block *)((uintptr_t)corrupted_ptr -
				      sizeof(struct heap_block));
	u32 original_magic = block->magic;
	block->magic = 0xDEADC0DE;

	status = heap_free(heap, corrupted_ptr);
	EXPECT_EQ(status, EINVAL); // Security check caught the mismatch

	// Restore original properties so heap context destruction sequences can complete smoothly
	block->magic = original_magic;

	heap_free(heap, corrupted_ptr);
	heap_destroy(heap);
	TEST_RESULT();
}
