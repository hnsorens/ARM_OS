#include "pt.h"
#include "../modules.h"
#include "../../include/api/mmu.h"
#include "../../include/api/pmm.h"
#include "../../include/errno.h"
#include "../utils.h"
#include "../../include/errno.h"

/* --- Linux Kernel Style Optimization & Predicate Macros --- */
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define pte_valid(pte) (!!((pte) & 1ULL))
#define u64able(pte) (((pte) & 0x3ULL) == ARM_TABLE_DESCRIPTOR)
#define IS_ALIGNED(x, a) (((x) & ((typeof(x))(a) - 1)) == 0)

#define PTE_FLAGS_MASK 0xFFFF000000000FFFULL
#define PTE_ADDR_MASK 0x0000FFFFFFFFF000ULL
#define HHDM_OFFSET 0xFFFF800000000000ULL
#define TLB_BATCH_THRESHOLD 64

EXTERN_IMPORT_INTERFACE(pmm, pmm);

static inline u64 *l2v(unsigned long phys)
{
	return (u64 *)(phys + HHDM_OFFSET);
}

static struct pt_indices extract_indices(unsigned long virt)
{
	struct pt_indices idx;

	idx.offset = virt & 0xFFF;
	idx.l3_index = P3_INDEX(virt);
	idx.l2_index = P2_INDEX(virt);
	idx.l1_index = P1_INDEX(virt);
	idx.l0_index = P0_INDEX(virt);

	return idx;
}

static bool is_pt_empty(unsigned long pt_phys)
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

static void free_l2_table(u64 *l2)
{
	u64 k;

	for (k = 0; k < 512; k++) {
		if (pte_valid(l2[k]) && u64able(l2[k]))
			pmm.free_page(0, l2[k] & PAGE_MASK);
	}
}

static void free_l1_table(u64 *l1)
{
	u64 *l2;
	u64 j;

	for (j = 0; j < 512; j++) {
		if (!pte_valid(l1[j]) || !u64able(l1[j]))
			continue;

		l2 = l2v(l1[j] & PAGE_MASK);
		free_l2_table(l2);
		pmm.free_page(0, l1[j] & PAGE_MASK);
	}
}

int pt_free(unsigned long root)
{
	u64 *l0, *l1;
	u64 i;

	if (unlikely(!root))
		return -EINVAL;

	l0 = l2v(root);
	for (i = 0; i < 512; i++) {
		if (!pte_valid(l0[i]) || !u64able(l0[i]))
			continue;

		l1 = l2v(l0[i] & PAGE_MASK);
		free_l1_table(l1);
		pmm.free_page(0, l0[i] & PAGE_MASK);
	}

	pmm.free_page(0, root);
	return 0;
}

/* --- SECTION 2: SYSTEM CLONE HELPERS (pt_copy) --- */

static void copy_l3_table(u64 *dst_l3, u64 *src_l3)
{
	u64 m;

	for (m = 0; m < 512; m++)
		dst_l3[m] = src_l3[m];
}

static int copy_l2_table(u64 *dst_l2, u64 *src_l2)
{
	unsigned long new_l3_phys;
	u64 *src_l3, *dst_l3;
	u64 k;

	for (k = 0; k < 512; k++) {
		if (!pte_valid(src_l2[k]))
			continue;

		if (!u64able(src_l2[k])) {
			dst_l2[k] = src_l2[k];
			continue;
		}

		if (unlikely(pmm.alloc_page(0, &new_l3_phys)))
			return -ENOMEM;

		dst_l2[k] = new_l3_phys | (src_l2[k] & ~PAGE_MASK);
		src_l3 = l2v(src_l2[k] & PAGE_MASK);
		dst_l3 = l2v(new_l3_phys);
		copy_l3_table(dst_l3, src_l3);
	}
	return 0;
}

static int copy_l1_table(u64 *dst_l1, u64 *src_l1)
{
	unsigned long new_l2_phys;
	u64 *src_l2, *dst_l2;
	u64 j;

	for (j = 0; j < 512; j++) {
		if (!pte_valid(src_l1[j]))
			continue;

		if (!u64able(src_l1[j])) {
			dst_l1[j] = src_l1[j];
			continue;
		}

		if (unlikely(pmm.alloc_page(0, &new_l2_phys)))
			return -ENOMEM;

		dst_l1[j] = new_l2_phys | (src_l1[j] & ~PAGE_MASK);
		src_l2 = l2v(src_l1[j] & PAGE_MASK);
		dst_l2 = l2v(new_l2_phys);
		kmemset(dst_l2, 0, 4096);

		if (unlikely(copy_l2_table(dst_l2, src_l2)))
			return -ENOMEM;
	}
	return 0;
}

int pt_copy(unsigned long src_root, unsigned long *dst_root)
{
	unsigned long new_l0_phys, new_l1_phys;
	u64 *src_l0, *dst_l0, *src_l1, *dst_l1;
	u64 i;

	if (unlikely(!src_root || !dst_root))
		return -EINVAL;

	if (unlikely(pmm.alloc_page(0, &new_l0_phys)))
		return -ENOMEM;

	src_l0 = l2v(src_root);
	dst_l0 = l2v(new_l0_phys);
	kmemset(dst_l0, 0, 4096);

	for (i = 0; i < 512; i++) {
		if (!pte_valid(src_l0[i]))
			continue;

		if (!u64able(src_l0[i])) {
			dst_l0[i] = src_l0[i];
			continue;
		}

		if (unlikely(pmm.alloc_page(0, &new_l1_phys)))
			goto fail;

		dst_l0[i] = new_l1_phys | (src_l0[i] & ~PAGE_MASK);
		src_l1 = l2v(src_l0[i] & PAGE_MASK);
		dst_l1 = l2v(new_l1_phys);
		kmemset(dst_l1, 0, 4096);

		if (unlikely(copy_l1_table(dst_l1, src_l1)))
			goto fail;
	}

	*dst_root = new_l0_phys;
	return 0;

fail:
	pt_free(new_l0_phys);
	return -ENOMEM;
}

/* --- SECTION 3: MAPPING ENGINE OPERATION HELPERS (pt_map) --- */

static int pt_map_single_page(u64 *l0, unsigned long vaddr, unsigned long paddr,
			      u64 pg_size, enum mmu_flags f)
{
	struct pt_indices idx = extract_indices(vaddr);
	u64 *l1, *l2, *l3;
	unsigned long tbl_phys;

	/* Resolve Level 0 -> Level 1 */
	if (!pte_valid(l0[idx.l0_index])) {
		if (unlikely(pmm.alloc_page(0, &tbl_phys)))
			return -ENOMEM;
		l1 = l2v(tbl_phys);
		kmemset(l1, 0, 4096);
		l0[idx.l0_index] = tbl_phys | ARM_TABLE_DESCRIPTOR;
	} else {
		l1 = l2v(l0[idx.l0_index] & PAGE_MASK);
	}

	if (pg_size == PS_1GB) {
		if (unlikely(pte_valid(l1[idx.l1_index])))
			return -EEXIST;
		l1[idx.l1_index] = paddr | f;
		return 0;
	}

	/* Resolve Level 1 -> Level 2 */
	if (!pte_valid(l1[idx.l1_index])) {
		if (unlikely(pmm.alloc_page(0, &tbl_phys)))
			return -ENOMEM;
		l2 = l2v(tbl_phys);
		kmemset(l2, 0, 4096);
		l1[idx.l1_index] = tbl_phys | ARM_TABLE_DESCRIPTOR;
	} else {
		l2 = l2v(l1[idx.l1_index] & PAGE_MASK);
	}

	if (pg_size == PS_2MB) {
		if (unlikely(pte_valid(l2[idx.l2_index])))
			return -EEXIST;
		l2[idx.l2_index] = paddr | f;
		return 0;
	}

	/* Resolve Level 2 -> Level 3 */
	if (!pte_valid(l2[idx.l2_index])) {
		if (unlikely(pmm.alloc_page(0, &tbl_phys)))
			return -ENOMEM;
		l3 = l2v(tbl_phys);
		kmemset(l3, 0, 4096);
		l2[idx.l2_index] = tbl_phys | ARM_TABLE_DESCRIPTOR;
	} else {
		l3 = l2v(l2[idx.l2_index] & PAGE_MASK);
	}

	if (unlikely(pte_valid(l3[idx.l3_index])))
		return -EEXIST;

	l3[idx.l3_index] = paddr | f | ARM_PAGE_DESCRIPTOR;
	return 0;
}

int pt_map(unsigned long root, unsigned long virt, unsigned long phys,
	   u64 pg_count, enum page_size pg_size, enum mmu_flags f)
{
	u64 *l0;
	u64 i;
	int err;

	if (unlikely(!root))
		return -EINVAL;
	if (unlikely(!IS_ALIGNED(virt, pg_size) || !IS_ALIGNED(phys, pg_size)))
		return -EINVAL;
	if (unlikely(virt + (pg_count * pg_size) < virt))
		return -EOVERFLOW;

	l0 = l2v(root);

	for (i = 0; i < pg_count; i++) {
		unsigned long curr_v = virt + (i * pg_size);
		unsigned long curr_p = phys + (i * pg_size);

		err = pt_map_single_page(l0, curr_v, curr_p, pg_size, f);
		if (unlikely(err))
			return err;
	}

	return pt_invalidate(virt, pg_count, pg_size);
}

/* --- SECTION 4: UNMAPPING ENGINE OPERATION HELPERS (pt_unmap) --- */

static void pt_unmap_single_page(u64 *l0, unsigned long vaddr, u64 pg_size)
{
	struct pt_indices idx = extract_indices(vaddr);
	unsigned long l1_phys, l2_phys, l3_phys;
	u64 *l1, *l2, *l3;

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
		pmm.free_page(0, l3_phys);
		l2[idx.l2_index] = 0;
	}

prune_l2:
	if (is_pt_empty(l2_phys)) {
		pmm.free_page(0, l2_phys);
		l1[idx.l1_index] = 0;
	}

prune_l1:
	if (is_pt_empty(l1_phys)) {
		pmm.free_page(0, l1_phys);
		l0[idx.l0_index] = 0;
	}
}

int pt_unmap(unsigned long root, unsigned long virt, u64 pg_count,
	     enum page_size pg_size)
{
	u64 *l0;
	u64 i;

	if (unlikely(!root))
		return -EINVAL;

	l0 = l2v(root);

	for (i = 0; i < pg_count; i++)
		pt_unmap_single_page(l0, virt + (i * pg_size), pg_size);

	return pt_invalidate(virt, pg_count, pg_size);
}

/* --- SECTION 5: ACCESS ATTRIBUTE MODIFICATION HELPERS (pt_protect) --- */

static int pt_protect_single_page(u64 *l0, unsigned long vaddr, u64 pg_size,
				  enum mmu_flags f)
{
	struct pt_indices idx = extract_indices(vaddr);
	unsigned long phys_addr;
	u64 *l1, *l2, *l3;

	if (!pte_valid(l0[idx.l0_index]))
		return -EFAULT;
	l1 = l2v(l0[idx.l0_index] & PAGE_MASK);

	if (pg_size == PS_1GB) {
		if (!pte_valid(l1[idx.l1_index]))
			return -EFAULT;
		phys_addr = l1[idx.l1_index] & PTE_ADDR_MASK;
		l1[idx.l1_index] = phys_addr | f;
		return 0;
	}

	if (!pte_valid(l1[idx.l1_index]))
		return -EFAULT;
	l2 = l2v(l1[idx.l1_index] & PAGE_MASK);

	if (pg_size == PS_2MB) {
		if (!pte_valid(l2[idx.l2_index]))
			return -EFAULT;
		phys_addr = l2[idx.l2_index] & PTE_ADDR_MASK;
		l2[idx.l2_index] = phys_addr | f;
		return 0;
	}

	if (!pte_valid(l2[idx.l2_index]))
		return -EFAULT;
	l3 = l2v(l2[idx.l2_index] & PAGE_MASK);

	if (pg_size == PS_4KB) {
		if (!pte_valid(l3[idx.l3_index]))
			return -EFAULT;
		phys_addr = l3[idx.l3_index] & PTE_ADDR_MASK;
		l3[idx.l3_index] = phys_addr | f | ARM_PAGE_DESCRIPTOR;
	}

	return 0;
}

int pt_protect(unsigned long root, unsigned long virt, u64 pg_count,
	       enum page_size pg_size, enum mmu_flags f)
{
	u64 *l0;
	u64 i;
	int err;

	if (unlikely(!root))
		return -EINVAL;

	l0 = l2v(root);

	for (i = 0; i < pg_count; i++) {
		err = pt_protect_single_page(l0, virt + (i * pg_size), pg_size,
					     f);
		if (unlikely(err))
			return err;
	}

	return pt_invalidate(virt, pg_count, pg_size);
}

/* --- SECTION 6: SYSTEM UTILITIES AND CORE API ENTRY POINTS --- */

int pt_alloc(u64 *out_root)
{
	if (unlikely(!out_root))
		return -EINVAL;

	return pmm.alloc_page(0, out_root);
}

int pt_set_user_ctx(unsigned long root, u16 asid)
{
	unsigned long ttbr = ((u64)asid << 48) | (root & PAGE_MASK);

	__asm__ volatile("msr ttbr0_el1, %0\n"
			 "isb"
			 :
			 : "r"(ttbr)
			 : "memory");
	return 0;
}

int pt_set_kernel_ctx(unsigned long root, u16 asid)
{
	unsigned long ttbr = ((u64)asid << 48) | (root & PAGE_MASK);

	__asm__ volatile("msr ttbr1_el1, %0\n"
			 "isb"
			 :
			 : "r"(ttbr)
			 : "memory");
	return 0;
}

int pt_translate(unsigned long root, unsigned long virt,
		 unsigned long *phys_out, enum mmu_flags *flags_out)
{
	struct pt_indices idx;
	unsigned long l1_phys, l2_phys, l3_phys, phys_base;
	u64 *l0, *l1, *l2, *l3;

	if (unlikely(!root || !phys_out || !flags_out))
		return -EINVAL;

	idx = extract_indices(virt);
	l0 = l2v(root);

	if (!pte_valid(l0[idx.l0_index]))
		return -EFAULT;

	l1_phys = l0[idx.l0_index] & PAGE_MASK;
	l1 = l2v(l1_phys);

	if (!(l1[idx.l1_index] & (1ULL << 1))) {
		if (!pte_valid(l1[idx.l1_index]))
			return -EFAULT;
		phys_base = l1[idx.l1_index] & ~0x3FFFFFFF;
		*phys_out = phys_base + (virt & 0x3FFFFFFF);
		*flags_out = l1[idx.l1_index] & PTE_FLAGS_MASK;
		return 0;
	}

	l2_phys = l1[idx.l1_index] & PAGE_MASK;
	l2 = l2v(l2_phys);

	if (!(l2[idx.l2_index] & (1ULL << 1))) {
		if (!pte_valid(l2[idx.l2_index]))
			return -EFAULT;
		phys_base = l2[idx.l2_index] & ~0x1FFFFF;
		*phys_out = phys_base + (virt & 0x1FFFFF);
		*flags_out = l2[idx.l2_index] & PTE_FLAGS_MASK;
		return 0;
	}

	l3_phys = l2[idx.l2_index] & PAGE_MASK;
	l3 = l2v(l3_phys);

	if (l3[idx.l3_index] & ARM_PAGE_DESCRIPTOR) {
		phys_base = l3[idx.l3_index] & PAGE_MASK;
		*phys_out = phys_base + idx.offset;
		*flags_out = l3[idx.l3_index] & PTE_FLAGS_MASK;
		return 0;
	}

	return -EFAULT;
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

	for (i = 0; i < pg_count; ++i) {
		unsigned long target = virt + (i * pg_size);
		unsigned long val = target >> 12;

		__asm__ volatile("tlbi vale1is, %0" : : "r"(val) : "memory");
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
