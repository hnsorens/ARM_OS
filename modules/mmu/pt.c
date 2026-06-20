/**
 * @file pt.c
 * @brief Core Implementation details of the AArch64 Multi-Level Translation Page Table Engine.
 */

#include "pt.h"
#include <modules.h>
#include <api/mmu.h>
#include <api/pmm.h>
#include <errno.h>
#include <utils.h>

/* --- Linux Kernel Style Optimization & Predicate Macros --- */
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define pte_valid(pte) (!!((pte) & 1ULL))
#define u64able(pte) (((pte) & 0x3ULL) == ARM_TABLE_DESCRIPTOR)
#define IS_ALIGNED(x, a) (((x) & ((typeof(x))(a) - 1)) == 0)

#define PTE_FLAGS_MASK 0xFFFF000000000FFFULL
#define PTE_RETURN_FLAG_MASK \
	(MMU_RO | MMU_USER | MMU_NO_EXEC | MMU_NOCACHE | MMU_WRITE_THROUGH)
#define PTE_ADDR_MASK 0x0000FFFFFFFFF000ULL
#define HHDM_OFFSET 0xFFFF800000000000ULL
#define TLB_BATCH_THRESHOLD 64

/* --- Core Module Linkage Hooks --- */
EXTERN_IMPORT_INTERFACE(pmm, pmm);

/**
 * @brief Internal Helper: Computes the virtual memory address alias via High Half Direct Map offsets.
 * @param[in] phys Base physical address coordinates to adjust.
 * @return u64* Pointer targeting the matching virtual address mirror window location.
 */
static inline u64 *l2v(u64 phys)
{
	return (u64 *)(phys + HHDM_OFFSET);
}

/**
 * @brief Internal Helper: Deconstructs a linear virtual address into relative multi-level page indexes.
 * @param[in] virt Raw virtual address address space coordinates to disassemble.
 * @return struct pt_indices Decoded architectural indexing metadata tracking structures.
 */
static struct pt_indices extract_indices(u64 virt)
{
	struct pt_indices idx;

	idx.offset = virt & 0xFFF;
	idx.l3_index = P3_INDEX(virt);
	idx.l2_index = P2_INDEX(virt);
	idx.l1_index = P1_INDEX(virt);
	idx.l0_index = P0_INDEX(virt);

	return idx;
}

/**
 * @brief Internal Helper: Inspects an active page directory frame to verify if all elements are dead.
 * @param[in] pt_phys Physical frame address targeting the memory lookup matrix to check.
 * @return bool True if every entry inside the structural tracking container is zero.
 */
static bool is_pt_empty(u64 pt_phys)
{
	u64 *table = l2v(pt_phys);
	u64 i;

	for (i = 0; i < 512; i++) {
		if (table[i] != 0)
			return false;
	}
	return true;
}

/* --- SECTION 1: SYSTEM TEARDOWN HELPERS (pt_free) --- */

/**
 * @brief Internal Helper: Traverses and wipes terminal entries across Level 2 descriptors.
 * @param[in,out] l2 Pointer addressing the base level 2 table matrix cluster.
 */
static void free_l2_table(u64 *l2)
{
	u64 k;

	for (k = 0; k < 512; k++) {
		if (pte_valid(l2[k]) && u64able(l2[k]))
			pmm.release(l2[k] & PAGE_MASK);
	}
}

/**
 * @brief Internal Helper: Traverses down Level 1 table descriptor matrices to clear ancestral links.
 * @param[in,out] l1 Pointer addressing the base level 1 table matrix structure.
 */
static void free_l1_table(u64 *l1)
{
	u64 *l2;
	u64 j;

	for (j = 0; j < 512; j++) {
		if (!pte_valid(l1[j]) || !u64able(l1[j]))
			continue;

		l2 = l2v(l1[j] & PAGE_MASK);
		free_l2_table(l2);
		pmm.release(l1[j] & PAGE_MASK);
	}
}

int pt_free(u64 root)
{
	int status = 0;
	u64 *l0 = NULL;
	u64 *l1 = NULL;
	u64 i;

	if (unlikely(!root)) {
		status = -EINVAL;
		goto cleanup;
	}

	l0 = l2v(root);
	for (i = 0; i < 512; i++) {
		if (!pte_valid(l0[i]) || !u64able(l0[i]))
			continue;

		l1 = l2v(l0[i] & PAGE_MASK);
		free_l1_table(l1);
		pmm.release(l0[i] & PAGE_MASK);
	}

	pmm.release(root);

cleanup:
	return status;
}

/* --- SECTION 2: SYSTEM CLONE HELPERS (pt_copy) --- */

/**
 * @brief Internal Helper: Performs an exact hardware duplicate clone of terminal Level 3 descriptors.
 * @param[out] dst_l3 Pointer tracking destination page map indices.
 * @param[in]  src_l3 Pointer targeting raw source model templates.
 */
static void copy_l3_table(u64 *dst_l3, u64 *src_l3)
{
	u64 m;

	for (m = 0; m < 512; m++)
		dst_l3[m] = src_l3[m];
}

/**
 * @brief Internal Helper: Iteratively mirrors layout schemas across active Level 2 structures.
 * @param[out] dst_l2 Tracking matrix pointing to target duplicate destination structures.
 * @param[in]  src_l2 Baseline operational framework matching template inputs.
 * @return int Operational execution status context values.
 */
static int copy_l2_table(u64 *dst_l2, u64 *src_l2)
{
	int status = 0;
	u64 new_l3_phys;
	u64 *src_l3 = NULL;
	u64 *dst_l3 = NULL;
	u64 k;

	for (k = 0; k < 512; k++) {
		if (!pte_valid(src_l2[k]))
			continue;

		if (!u64able(src_l2[k])) {
			dst_l2[k] = src_l2[k];
			continue;
		}

		status = pmm.alloc_page(0, &new_l3_phys);
		if (unlikely(status)) {
			status = -ENOMEM;
			goto cleanup;
		}

		dst_l2[k] = new_l3_phys | ARM_TABLE_DESCRIPTOR;

		src_l3 = l2v(src_l2[k] & PAGE_MASK);
		dst_l3 = l2v(new_l3_phys);
		kmemset(dst_l3, 0, 4096);

		copy_l3_table(dst_l3, src_l3);
	}

cleanup:
	return status;
}

/**
 * @brief Internal Helper: Deep copies architectural frameworks tracking Level 1 page hierarchies.
 * @param[out] dst_l1 Destination pointer target mapping clone storage directories.
 * @param[in]  src_l1 Master structural source map layout configurations.
 * @return int Operational execution status context values.
 */
static int copy_l1_table(u64 *dst_l1, u64 *src_l1)
{
	int status = 0;
	u64 new_l2_phys;
	u64 *src_l2 = NULL;
	u64 *dst_l2 = NULL;
	u64 j;

	for (j = 0; j < 512; j++) {
		if (!pte_valid(src_l1[j]))
			continue;

		if (!u64able(src_l1[j])) {
			dst_l1[j] = src_l1[j];
			continue;
		}

		status = pmm.alloc_page(0, &new_l2_phys);
		if (unlikely(status)) {
			status = -ENOMEM;
			goto cleanup;
		}

		dst_l1[j] = new_l2_phys | ARM_TABLE_DESCRIPTOR;

		src_l2 = l2v(src_l1[j] & PAGE_MASK);
		dst_l2 = l2v(new_l2_phys);
		kmemset(dst_l2, 0, 4096);

		status = copy_l2_table(dst_l2, src_l2);
		if (unlikely(status)) {
			goto cleanup;
		}
	}

cleanup:
	return status;
}

int pt_copy(u64 src_root, u64 *dst_root)
{
	int status = 0;
	u64 new_l0_phys = 0;
	u64 new_l1_phys = 0;
	u64 *src_l0 = NULL;
	u64 *dst_l0 = NULL;
	u64 *src_l1 = NULL;
	u64 *dst_l1 = NULL;
	u64 i;

	if (unlikely(!src_root || !dst_root)) {
		status = -EINVAL;
		goto cleanup;
	}

	status = pmm.alloc_page(0, &new_l0_phys);
	if (unlikely(status)) {
		status = -ENOMEM;
		goto cleanup;
	}

	src_l0 = l2v(src_root);
	dst_l0 = l2v(new_l0_phys);
	kmemset(dst_l0, 0, 4096);

	for (i = 0; i < 512; i++) {
		if (!pte_valid(src_l0[i]))
			continue;

		status = pmm.alloc_page(0, &new_l1_phys);
		if (unlikely(status)) {
			status = -ENOMEM;
			goto fail_unwind;
		}

		dst_l0[i] = new_l1_phys | ARM_TABLE_DESCRIPTOR;

		src_l1 = l2v(src_l0[i] & PAGE_MASK);
		dst_l1 = l2v(new_l1_phys);
		kmemset(dst_l1, 0, 4096);

		status = copy_l1_table(dst_l1, src_l1);
		if (unlikely(status)) {
			goto fail_unwind;
		}
	}

	*dst_root = new_l0_phys;
	goto cleanup;

fail_unwind:
	pt_free(new_l0_phys);

cleanup:
	return status;
}

/* --- SECTION 3: MAPPING ENGINE OPERATION HELPERS (pt_map) --- */

/**
 * @brief Internal Helper: Traverses tree nodes to establish a single targeted address entry link.
 * @param[in,out] l0 Base memory coordinate anchoring root directory structures.
 * @param[in]     vaddr Virtual translation route path coordinate destination.
 * @param[in]     paddr Physical system target core destination offset frame address.
 * @param[in]     pg_size Discrete architectural geometry constraint flag.
 * @param[in]     f System operational access flags profile configuration bitmask.
 * @return int Operational execution status context values.
 */
static int pt_map_single_page(u64 *l0, u64 vaddr, u64 paddr, u64 pg_size,
			      enum mmu_flags f)
{
	int status = 0;
	struct pt_indices idx = extract_indices(vaddr);
	u64 *l1 = NULL;
	u64 *l2 = NULL;
	u64 *l3 = NULL;
	u64 tbl_phys;

	/* Resolve Level 0 -> Level 1 Directory Transitions */
	if (!pte_valid(l0[idx.l0_index])) {
		status = pmm.alloc_page(0, &tbl_phys);
		if (unlikely(status)) {
			status = -ENOMEM;
			goto cleanup;
		}
		l1 = l2v(tbl_phys);
		kmemset(l1, 0, 4096);
		l0[idx.l0_index] = tbl_phys | ARM_TABLE_DESCRIPTOR;
	} else {
		l1 = l2v(l0[idx.l0_index] & PAGE_MASK);
	}

	/* Process Massive 1GB Monolithic Block Allocations directly inside Level 1 structures */
	if (pg_size == PS_1GB) {
		if (unlikely(pte_valid(l1[idx.l1_index]))) {
			status = -EEXIST;
			goto cleanup;
		}
		l1[idx.l1_index] = (paddr & ~0x3FFFFFFF) |
				   ((f & ~(1ULL << 1)) | 1ULL);
		goto cleanup;
	}

	/* Resolve Level 1 -> Level 2 Directory Transitions */
	if (!pte_valid(l1[idx.l1_index])) {
		status = pmm.alloc_page(0, &tbl_phys);
		if (unlikely(status)) {
			status = -ENOMEM;
			goto cleanup;
		}
		l2 = l2v(tbl_phys);
		kmemset(l2, 0, 4096);
		l1[idx.l1_index] = tbl_phys | ARM_TABLE_DESCRIPTOR;
	} else {
		l2 = l2v(l1[idx.l1_index] & PAGE_MASK);
	}

	/* Process Midsize 2MB Block Allocations inside Level 2 boundaries safely */
	if (pg_size == PS_2MB) {
		if (unlikely(pte_valid(l2[idx.l2_index]))) {
			status = -EEXIST;
			goto cleanup;
		}
		l2[idx.l2_index] = (paddr & ~0x1FFFFF) |
				   ((f & ~(1ULL << 1)) | 1ULL);
		goto cleanup;
	}

	/* Resolve Level 2 -> Level 3 Terminal Frame Transitions */
	if (!pte_valid(l2[idx.l2_index])) {
		status = pmm.alloc_page(0, &tbl_phys);
		if (unlikely(status)) {
			status = -ENOMEM;
			goto cleanup;
		}
		l3 = l2v(tbl_phys);
		kmemset(l3, 0, 4096);
		l2[idx.l2_index] = tbl_phys | ARM_TABLE_DESCRIPTOR;
	} else {
		l3 = l2v(l2[idx.l2_index] & PAGE_MASK);
	}

	if (unlikely(pte_valid(l3[idx.l3_index]))) {
		status = -EEXIST;
		goto cleanup;
	}

	/* Finalize 4KB Standard Granule Page link parameters securely */
	l3[idx.l3_index] = (paddr & PAGE_MASK) | f | ARM_PAGE_DESCRIPTOR;

cleanup:
	return status;
}

int pt_map(u64 root, u64 virt, u64 phys, u64 pg_count, enum page_size pg_size,
	   enum mmu_flags f)
{
	int status = 0;
	u64 *l0 = NULL;
	u64 i;

	if (unlikely(!root)) {
		status = -EINVAL;
		goto cleanup;
	}
	if (unlikely(!IS_ALIGNED(virt, pg_size) ||
		     !IS_ALIGNED(phys, pg_size))) {
		status = -EINVAL;
		goto cleanup;
	}
	if (unlikely(virt + (pg_count * pg_size) < virt)) {
		status = -EOVERFLOW;
		goto cleanup;
	}

	l0 = l2v(root);

	for (i = 0; i < pg_count; i++) {
		u64 curr_v = virt + (i * pg_size);
		u64 curr_p = phys + (i * pg_size);

		status = pt_map_single_page(l0, curr_v, curr_p, pg_size, f);
		if (unlikely(status)) {
			goto cleanup;
		}
	}

	status = pt_invalidate(virt, pg_count, pg_size);

cleanup:
	return status;
}

/* --- SECTION 4: UNMAPPING ENGINE OPERATION HELPERS (pt_unmap) --- */

/**
 * @brief Internal Helper: Drops individual translation routes out of the active hardware matrix maps.
 * @param[in,out] l0 Base pointer referencing root level address directory anchors.
 * @param[in]     vaddr Virtual system mapping target address to systematically unmap.
 * @param[in]     pg_size Sizing footprint metrics classifying targeted allocations.
 */
static void pt_unmap_single_page(u64 *l0, u64 vaddr, u64 pg_size)
{
	struct pt_indices idx = extract_indices(vaddr);
	u64 l1_phys, l2_phys, l3_phys;
	u64 *l1 = NULL;
	u64 *l2 = NULL;
	u64 *l3 = NULL;

	if (!pte_valid(l0[idx.l0_index]))
		return;
	l1_phys = l0[idx.l0_index] & PAGE_MASK;
	l1 = l2v(l1_phys);

	if (pg_size == PS_1GB) {
		l1[idx.l1_index] = 0;
		goto prune_l1;
	}

	if (!pte_valid(l1[idx.l1_index]))
		return;
	l2_phys = l1[idx.l1_index] & PAGE_MASK;
	l2 = l2v(l2_phys);

	if (pg_size == PS_2MB) {
		l2[idx.l2_index] = 0;
		goto prune_l2;
	}

	if (!pte_valid(l2[idx.l2_index]))
		return;
	l3_phys = l2[idx.l2_index] & PAGE_MASK;
	l3 = l2v(l3_phys);

	if (pg_size == PS_4KB)
		l3[idx.l3_index] = 0;

	if (is_pt_empty(l3_phys)) {
		pmm.release(l3_phys);
		l2[idx.l2_index] = 0;
	}

prune_l2:
	if (is_pt_empty(l2_phys)) {
		pmm.release(l2_phys);
		l1[idx.l1_index] = 0;
	}

prune_l1:
	if (is_pt_empty(l1_phys)) {
		pmm.release(l1_phys);
		l0[idx.l0_index] = 0;
	}
}

int pt_unmap(u64 root, u64 virt, u64 pg_count, enum page_size pg_size)
{
	int status = 0;
	u64 *l0 = NULL;
	u64 i;

	if (unlikely(!root)) {
		status = -EINVAL;
		goto cleanup;
	}

	l0 = l2v(root);

	for (i = 0; i < pg_count; i++)
		pt_unmap_single_page(l0, virt + (i * pg_size), pg_size);

	status = pt_invalidate(virt, pg_count, pg_size);

cleanup:
	return status;
}

/* --- SECTION 5: ACCESS ATTRIBUTE MODIFICATION HELPERS (pt_protect) --- */

/**
 * @brief Internal Helper: Edits target access bitwise fields within single map tracking descriptor entries.
 * @param[in,out] l0 Base root memory alignment array pointer mapping context tracking chains.
 * @param[in]     vaddr Virtual index translation track target route coordinates.
 * @param[in]     pg_size Target sizing metrics mapping structural alignment layout steps.
 * @param[in]     f Modified flags configuration profiles to safely apply over active entries.
 * @return int Operational execution status context values.
 */
static int pt_protect_single_page(u64 *l0, u64 vaddr, u64 pg_size,
				  enum mmu_flags f)
{
	int status = 0;
	struct pt_indices idx = extract_indices(vaddr);
	u64 phys_addr;
	u64 *l1 = NULL;
	u64 *l2 = NULL;
	u64 *l3 = NULL;

	if (!pte_valid(l0[idx.l0_index])) {
		status = -EFAULT;
		goto cleanup;
	}
	l1 = l2v(l0[idx.l0_index] & PAGE_MASK);

	if (pg_size == PS_1GB) {
		if (!pte_valid(l1[idx.l1_index])) {
			status = -EFAULT;
			goto cleanup;
		}
		phys_addr = l1[idx.l1_index] & PTE_ADDR_MASK;
		l1[idx.l1_index] = phys_addr | f;
		goto cleanup;
	}

	if (!pte_valid(l1[idx.l1_index])) {
		status = -EFAULT;
		goto cleanup;
	}
	l2 = l2v(l1[idx.l1_index] & PAGE_MASK);

	if (pg_size == PS_2MB) {
		if (!pte_valid(l2[idx.l2_index])) {
			status = -EFAULT;
			goto cleanup;
		}
		phys_addr = l2[idx.l2_index] & PTE_ADDR_MASK;
		l2[idx.l2_index] = phys_addr | f;
		goto cleanup;
	}

	if (!pte_valid(l2[idx.l2_index])) {
		status = -EFAULT;
		goto cleanup;
	}
	l3 = l2v(l2[idx.l2_index] & PAGE_MASK);

	if (pg_size == PS_4KB) {
		if (!pte_valid(l3[idx.l3_index])) {
			status = -EFAULT;
			goto cleanup;
		}
		phys_addr = l3[idx.l3_index] & PTE_ADDR_MASK;
		l3[idx.l3_index] = phys_addr | f | ARM_PAGE_DESCRIPTOR;
	}

cleanup:
	return status;
}

int pt_protect(u64 root, u64 virt, u64 pg_count, enum page_size pg_size,
	       enum mmu_flags f)
{
	int status = 0;
	u64 *l0 = NULL;
	u64 i;

	if (unlikely(!root)) {
		status = -EINVAL;
		goto cleanup;
	}

	l0 = l2v(root);

	for (i = 0; i < pg_count; i++) {
		status = pt_protect_single_page(l0, virt + (i * pg_size),
						pg_size, f);
		if (unlikely(status)) {
			goto cleanup;
		}
	}

	status = pt_invalidate(virt, pg_count, pg_size);

cleanup:
	return status;
}

/* --- SECTION 6: SYSTEM UTILITIES AND CORE API ENTRY POINTS --- */

int pt_alloc(u64 *out_root)
{
	int status = 0;

	if (unlikely(!out_root)) {
		status = -EINVAL;
		goto cleanup;
	}

	status = pmm.alloc_page(0, out_root);
	if (status == 0) {
		kmemset((void *)*out_root, 0, 4096);
	}

cleanup:
	return status;
}

int pt_set_user_ctx(u64 root, u16 asid)
{
	u64 ttbr = ((u64)asid << 48) | (root & PAGE_MASK);

	__asm__ volatile("msr ttbr0_el1, %0\n"
			 "isb"
			 :
			 : "r"(ttbr)
			 : "memory");
	return 0;
}

int pt_set_kernel_ctx(u64 root, u16 asid)
{
	u64 ttbr = ((u64)asid << 48) | (root & PAGE_MASK);

	__asm__ volatile("msr ttbr1_el1, %0\n"
			 "isb"
			 :
			 : "r"(ttbr)
			 : "memory");
	return 0;
}

int pt_get_user_ctx(u64 *root)
{
	u64 ttbr0;
	__asm__ volatile("mrs %0, ttbr0_el1" : "=r"(ttbr0));
	*root = ttbr0 & PAGE_MASK;
	return 0;
}

int pt_get_kernel_ctx(u64 *root)
{
	u64 ttbr1;
	__asm__ volatile("mrs %0, ttbr1_el1" : "=r"(ttbr1));
	*root = ttbr1 & PAGE_MASK;
	return 0;
}

int pt_translate(u64 root, u64 virt, u64 *phys_out, enum mmu_flags *flags_out)
{
	int status = 0;
	struct pt_indices idx;
	u64 l1_phys, l2_phys, l3_phys, phys_base;
	u64 *l0 = NULL;
	u64 *l1 = NULL;
	u64 *l2 = NULL;
	u64 *l3 = NULL;

	if (unlikely(!root || !phys_out || !flags_out)) {
		status = -EINVAL;
		goto cleanup;
	}

	idx = extract_indices(virt);
	l0 = l2v(root);

	if (!pte_valid(l0[idx.l0_index])) {
		status = -EFAULT;
		goto cleanup;
	}

	l1_phys = l0[idx.l0_index] & PAGE_MASK;
	l1 = l2v(l1_phys);

	if (!pte_valid(l1[idx.l1_index])) {
		status = -EFAULT;
		goto cleanup;
	}

	/* Process Massive 1GB Block Translation Path Heuristics */
	if (!(l1[idx.l1_index] & (1ULL << 1))) {
		phys_base = l1[idx.l1_index] & ~0x3FFFFFFF;
		*phys_out = phys_base + (virt & 0x3FFFFFFF);
		*flags_out = l1[idx.l1_index] & PTE_RETURN_FLAG_MASK;
		goto cleanup;
	}

	l2_phys = l1[idx.l1_index] & PAGE_MASK;
	l2 = l2v(l2_phys);

	if (!pte_valid(l2[idx.l2_index])) {
		status = -EFAULT;
		goto cleanup;
	}

	/* Process Midsize 2MB Block Translation Path Heuristics */
	if (!(l2[idx.l2_index] & (1ULL << 1))) {
		phys_base = l2[idx.l2_index] & ~0x1FFFFF;
		*phys_out = phys_base + (virt & 0x1FFFFF);
		*flags_out = l2[idx.l2_index] & PTE_RETURN_FLAG_MASK;
		goto cleanup;
	}

	l3_phys = l2[idx.l2_index] & PAGE_MASK;
	l3 = l2v(l3_phys);

	if (!pte_valid(l3[idx.l3_index])) {
		status = -EFAULT;
		goto cleanup;
	}

	/* Process Standard 4KB Page Translation Terminal Map Attributes */
	if (l3[idx.l3_index] & ARM_PAGE_DESCRIPTOR) {
		phys_base = l3[idx.l3_index] & PAGE_MASK;
		*phys_out = phys_base + idx.offset;
		*flags_out = l3[idx.l3_index] & PTE_RETURN_FLAG_MASK;
		goto cleanup;
	}

	status = -EFAULT;

cleanup:
	return status;
}

int pt_flush(void)
{
	__asm__ volatile("tlbi vmalle1is\n"
			 "dsb ish\n"
			 "isb"
			 :
			 :
			 : "memory");
	return 0;
}

int pt_invalidate(u64 virt, u64 pg_count, enum page_size pg_size)
{
	u64 i;

	if (pg_count > TLB_BATCH_THRESHOLD) {
		__asm__ volatile("tlbi vmalle1is\n"
				 "dsb ish\n"
				 "isb"
				 :
				 :
				 : "memory");
		return 0;
	}

	u64 ttbr0;
	__asm__ volatile("mrs %0, ttbr0_el1" : "=r"(ttbr0));
	u64 asid = (ttbr0 >> 48) & 0xFFFFULL;

	for (i = 0; i < pg_count; ++i) {
		u64 target = virt + (i * pg_size);

		/* * AArch64 Architectural TLBI Payload Formatter Map:
         * Bits [43:0]  = Target Virtual Address range payload segments [55:12]
         * Bits [63:48] = Assigned Address Space Identifier (ASID) tag code context
         */
		u64 tlbi_payload = ((target >> 12) & 0x000000FFFFFFFFFFULL);
		tlbi_payload |= (asid << 48);

		if (target >= 0xFFFF800000000000ULL) {
			__asm__ volatile("tlbi vae1is, %0"
					 :
					 : "r"(tlbi_payload)
					 : "memory");
		} else {
			__asm__ volatile("tlbi vale1is, %0"
					 :
					 : "r"(tlbi_payload)
					 : "memory");
		}
	}

	__asm__ volatile("dsb ish\n"
			 "isb"
			 :
			 :
			 : "memory");
	return 0;
}

int pt_set_mair(u64 mair)
{
	__asm__ volatile("msr mair_el1, %0\n"
			 "isb"
			 :
			 : "r"(mair)
			 : "memory");
	return 0;
}
