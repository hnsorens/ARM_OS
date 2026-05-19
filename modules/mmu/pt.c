#include "pt.h"
#include "../modules.h"
#include "../../include/api/pmm.h"
#include "../utils.h"

/* Linkage reference to external physical memory manager interface */
EXTERN_IMPORT_INTERFACE(pmm, pmm);

/* --- Architectural Bitmasks and Virtual Translation Constants --- */
#define PTE_FLAGS_MASK 0xFFFF000000000FFFULL
#define PTE_ADDR_MASK 0x0000FFFFFFFFF000ULL
#define HHDM_OFFSET 0xFFFF800000000000ULL

/* Helper address arithmetic translation inline routines */
static inline pte_t *l2v(paddr_t phys)
{
	return (pte_t *)(phys + HHDM_OFFSET);
}

/* Splits a virtual address into distinct page table indexing fields */
static pt_indices_t extract_indices(vaddr_t virt)
{
	pt_indices_t idx;

	idx.offset = virt & 0xFFF; /* Page offset (Bits 0-11) */
	idx.l3_index = P3_INDEX(virt); /* Page Table index (Bits 12-20) */
	idx.l2_index = P2_INDEX(virt); /* Page Directory index (Bits 21-29) */
	idx.l1_index = P1_INDEX(virt); /* PDPT index (Bits 30-38) */
	idx.l0_index = P0_INDEX(virt); /* PML4 index (Bits 39-47) */

	return idx;
}

/* Allocates an empty 4KB physical page to act as a root page table */
k_status_t pt_alloc(paddr_t *out_root)
{
	return pmm.alloc_page(0, out_root);
}

/* Destroys and cleans up nested page tables recursively */
static void pte_recursive_free(paddr_t pt_phys, int level)
{
	pte_t *pt = l2v(pt_phys);

	/* Only recurse if we aren't at the bottom level (L3 leaf pages) */
	if (level < 3) {
		for (size_t i = 0; i < 512; i++) {
			pte_t pte = pt[i];

			/* Check if it's a valid table descriptor (Bit 1 must be 1 for L0-L2) */
			if ((pte & 1) && (pte & ARM_TABLE_DESCRIPTOR)) {
				paddr_t child_phys = pte & PAGE_MASK;
				pte_recursive_free(child_phys, level + 1);
			}
		}
	}

	/* Free the current table frame page back to the PMM */
	pmm.free_page(0, pt_phys);
}

/* Completely frees an entire page directory tracking tree context */
k_status_t pt_free(paddr_t root)
{
	if (!root)
		return K_STATUS_BAD_PHYS_ADDR;

	/* Start recursive structural cleanup sequence from Level 0 */
	pte_recursive_free(root, 0);
	return K_STATUS_OK;
}

/* Recursively creates a deep copy of an active translation directory tree */
static k_status_t pte_deep_copy(paddr_t src_phys, paddr_t *dst_phys, int level)
{
	k_status_t status;
	paddr_t new_page_phys;

	/* Allocate an independent page frame to host the cloned directory layer */
	status = pmm.alloc_page(0, &new_page_phys);
	if (k_error(status))
		return status;

	pte_t *src = l2v(src_phys);
	pte_t *dst = l2v(new_page_phys);

	kmemset(dst, 0, 4096);

	for (size_t i = 0; i < 512; i++) {
		if (!(src[i] & 1))
			continue;

		/* Process sub-directory structures or directly duplicate absolute map leaf links */
		if ((src[i] & ARM_TABLE_DESCRIPTOR) && level < 3) {
			paddr_t child_dst_phys;
			status = pte_deep_copy(src[i] & PAGE_MASK,
					       &child_dst_phys, level + 1);
			if (k_error(status))
				return status;

			/* Preserve original flags combined with the updated child physical address */
			dst[i] = child_dst_phys | (src[i] & ~PAGE_MASK);
		} else {
			dst[i] = src[i];
		}
	}

	*dst_phys = new_page_phys;
	return K_STATUS_OK;
}

/* Generates an independent clone of an active mapping configuration environment */
k_status_t pt_copy(paddr_t src_root, paddr_t *dst_root)
{
	if (!src_root || !dst_root)
		return K_STATUS_BAD_PHYS_ADDR;
	return pte_deep_copy(src_root, dst_root, 0);
}

/* Configures and binds user mapping context into the hardware context registers */
k_status_t pt_set_user_ctx(paddr_t root, asid_t asid)
{
	/* AArch64 ASID lives in bits [63:48] of TTBR0_EL1 alongside the physical root pointer */
	uint64_t ttbr = ((uint64_t)asid << 48) | (root & PAGE_MASK);

	__asm__ volatile("msr ttbr0_el1, %0" : : "r"(ttbr));
	__asm__ volatile("isb");

	return K_STATUS_OK;
}

/* Configures and binds secure kernel memory execution paths into hardware slots */
k_status_t pt_set_kernel_ctx(paddr_t root, asid_t asid)
{
	/* AArch64 ASID lives in bits [63:48] of TTBR1_EL1 alongside the physical root pointer */
	uint64_t ttbr = ((uint64_t)asid << 48) | (root & PAGE_MASK);

	__asm__ volatile("msr ttbr1_el1, %0" : : "r"(ttbr));
	__asm__ volatile("isb");

	return K_STATUS_OK;
}

/* Generates coherent structural mappings linking virtual ranges to physical target sectors */
k_status_t pt_map(paddr_t root, vaddr_t virt, paddr_t phys, uint64_t pg_count,
		  page_size_t pg_size, mmu_flags_t f)
{
	if (!root)
		return K_STATUS_BAD_PHYS_ADDR;

	pte_t *l0 = l2v(root);

	for (uint64_t i = 0; i < pg_count; i++) {
		vaddr_t curr_vaddr = virt + (i * pg_size);
		paddr_t curr_phys = phys + (i * pg_size);
		pt_indices_t idx = extract_indices(curr_vaddr);

		/* --- Level 0 -> Level 1 Processing Loop Sequence --- */
		pte_t *l1;
		pte_t l0_pte = l0[idx.l0_index];

		if (!(l0_pte & 1)) {
			paddr_t l1_phys;
			if (k_error(pmm.alloc_page(0, &l1_phys)))
				return K_STATUS_OUT_OF_MEMORY;

			l1 = l2v(l1_phys);
			kmemset(l1, 0, 4096);
			l0[idx.l0_index] = l1_phys | ARM_TABLE_DESCRIPTOR;
		} else {
			l1 = l2v(l0_pte & PAGE_MASK);
		}

		/* Register 1GB huge page blocks directly into Level 1 slots */
		if (pg_size == PS_1GB) {
			l1[idx.l1_index] = curr_phys | f;
			continue;
		}

		/* --- Level 1 -> Level 2 Processing Loop Sequence --- */
		pte_t *l2;
		pte_t l1_pte = l1[idx.l1_index];

		if (!(l1_pte & 1)) {
			paddr_t l2_phys;
			if (k_error(pmm.alloc_page(0, &l2_phys)))
				return K_STATUS_OUT_OF_MEMORY;

			l2 = l2v(l2_phys);
			kmemset(l2, 0, 4096);
			l1[idx.l1_index] = l2_phys | ARM_TABLE_DESCRIPTOR;
		} else {
			l2 = l2v(l1_pte & PAGE_MASK);
		}

		/* Register 2MB large page blocks directly into Level 2 slots */
		if (pg_size == PS_2MB) {
			l2[idx.l2_index] = curr_phys | f;
			continue;
		}

		/* --- Level 2 -> Level 3 Processing Loop Sequence --- */
		pte_t *l3;
		pte_t l2_pte = l2[idx.l2_index];

		if (!(l2_pte & 1)) {
			paddr_t l3_phys;
			if (k_error(pmm.alloc_page(0, &l3_phys)))
				return K_STATUS_OUT_OF_MEMORY;

			l3 = l2v(l3_phys);
			kmemset(l3, 0, 4096);
			l2[idx.l2_index] = l3_phys | ARM_TABLE_DESCRIPTOR;
		} else {
			l3 = l2v(l2_pte & PAGE_MASK);
		}

		/* --- Level 3 Page Final Base Page Leaf Entry Initialization --- */
		l3[idx.l3_index] = curr_phys | f | ARM_PAGE_DESCRIPTOR;
	}

	return pt_invalidate(virt, pg_count, pg_size);
}

/* Scans a 4KB page directory table frame to verify if all entries are blank */
static bool is_pt_empty(paddr_t pt_phys)
{
	pte_t *table = l2v(pt_phys);
	for (size_t i = 0; i < 512; i++) {
		if (table[i] != 0)
			return false;
	}
	return true;
}

/* Tears down page map links across virtual spaces and dynamically prunes unneeded tables */
k_status_t pt_unmap(paddr_t root, vaddr_t virt, uint64_t pg_count,
		    page_size_t pg_size)
{
	if (!root)
		return K_STATUS_BAD_PHYS_ADDR;

	pte_t *l0 = l2v(root);

	for (size_t i = 0; i < pg_count; i++) {
		vaddr_t curr_vaddr = virt + i * pg_size;
		pt_indices_t idx = extract_indices(curr_vaddr);

		/* --- Level 0 -> Level 1 Processing Track --- */
		if (!(l0[idx.l0_index] & 1))
			continue;
		paddr_t l1_phys = l0[idx.l0_index] & PAGE_MASK;
		pte_t *l1 = l2v(l1_phys);

		if (pg_size == PS_1GB) {
			l1[idx.l1_index] = 0;
		} else {
			/* --- Level 1 -> Level 2 Processing Track --- */
			if (!(l1[idx.l1_index] & 1))
				continue;
			paddr_t l2_phys = l1[idx.l1_index] & PAGE_MASK;
			pte_t *l2 = l2v(l2_phys);

			if (pg_size == PS_2MB) {
				l2[idx.l2_index] = 0;
			} else {
				/* --- Level 2 -> Level 3 Processing Track --- */
				if (!(l2[idx.l2_index] & 1))
					continue;
				paddr_t l3_phys = l2[idx.l2_index] & PAGE_MASK;
				pte_t *l3 = l2v(l3_phys);

				if (pg_size == PS_4KB) {
					l3[idx.l3_index] = 0;
				}

				/* PRUNING: Release Level 3 table if completely empty */
				if (is_pt_empty(l3_phys)) {
					pmm.free_page(0, l3_phys);
					l2[idx.l2_index] = 0;
				}
			}

			/* PRUNING: Release Level 2 directory structure if empty */
			if (is_pt_empty(l2_phys)) {
				pmm.free_page(0, l2_phys);
				l1[idx.l1_index] = 0;
			}
		}

		/* PRUNING: Collapse the structural backbone Level 1 table if empty */
		if (is_pt_empty(l1_phys)) {
			pmm.free_page(0, l1_phys);
			l0[idx.l0_index] = 0;
		}
	}

	return pt_invalidate(virt, pg_count, pg_size);
}

/* Adjusts attribute protection settings across target virtual translation blocks */
k_status_t pt_protect(paddr_t root, vaddr_t virt, uint64_t pg_count,
		      page_size_t pg_size, mmu_flags_t f)
{
	if (!root)
		return K_STATUS_BAD_PHYS_ADDR;

	pte_t *l0 = l2v(root);

	for (size_t i = 0; i < pg_count; i++) {
		vaddr_t curr_vaddr = virt + i * pg_size;
		pt_indices_t idx = extract_indices(curr_vaddr);

		if (!(l0[idx.l0_index] & 1))
			return K_STATUS_NOT_MAPPED;
		pte_t *l1 = l2v(l0[idx.l0_index] & PAGE_MASK);

		if (pg_size == PS_1GB) {
			if (!(l1[idx.l1_index] & 1))
				return K_STATUS_NOT_MAPPED;
			paddr_t phys_addr = l1[idx.l1_index] & PTE_ADDR_MASK;
			l1[idx.l1_index] = phys_addr | f;
			continue;
		}

		if (!(l1[idx.l1_index] & 1))
			return K_STATUS_NOT_MAPPED;
		pte_t *l2 = l2v(l1[idx.l1_index] & PAGE_MASK);

		if (pg_size == PS_2MB) {
			if (!(l2[idx.l2_index] & 1))
				return K_STATUS_NOT_MAPPED;
			paddr_t phys_addr = l2[idx.l2_index] & PTE_ADDR_MASK;
			l2[idx.l2_index] = phys_addr | f;
			continue;
		}

		if (!(l2[idx.l2_index] & 1))
			return K_STATUS_NOT_MAPPED;
		pte_t *l3 = l2v(l2[idx.l2_index] & PAGE_MASK);

		if (pg_size == PS_4KB) {
			if (!(l3[idx.l3_index] & 1))
				return K_STATUS_NOT_MAPPED;
			paddr_t phys_addr = l3[idx.l3_index] & PTE_ADDR_MASK;
			l3[idx.l3_index] = phys_addr | f | ARM_PAGE_DESCRIPTOR;
		}
	}

	return pt_invalidate(virt, pg_count, pg_size);
}

/* Manually resolves custom virtual path lines down to physical hardware addresses */
k_status_t pt_translate(paddr_t root, vaddr_t virt, paddr_t *phys_out,
			mmu_flags_t *flags_out)
{
	if (!root || !phys_out || !flags_out)
		return K_STATUS_BAD_PHYS_ADDR;

	pt_indices_t idx = extract_indices(virt);
	pte_t *l0 = l2v(root);

	if (!(l0[idx.l0_index] & 1))
		return K_STATUS_NOT_MAPPED;

	paddr_t l1_phys = l0[idx.l0_index] & PAGE_MASK;
	pte_t *l1 = l2v(l1_phys);

	/* Level 1 Evaluation (1GB Block Check) */
	if (!(l1[idx.l1_index] & (1ULL << 1))) {
		if (!(l1[idx.l1_index] & 1))
			return K_STATUS_NOT_MAPPED;

		paddr_t phys_base = l1[idx.l1_index] & ~0x3FFFFFFF;
		*phys_out = phys_base + (virt & 0x3FFFFFFF);
		*flags_out = l1[idx.l1_index] & PTE_FLAGS_MASK;
		return K_STATUS_OK;
	}

	paddr_t l2_phys = l1[idx.l1_index] & PAGE_MASK;
	pte_t *l2 = l2v(l2_phys);

	/* Level 2 Evaluation (2MB Block Check) */
	if (!(l2[idx.l2_index] & (1ULL << 1))) {
		if (!(l2[idx.l2_index] & 1))
			return K_STATUS_NOT_MAPPED;

		paddr_t phys_base = l2[idx.l2_index] & ~0x1FFFFF;
		*phys_out = phys_base + (virt & 0x1FFFFF);
		*flags_out = l2[idx.l2_index] & PTE_FLAGS_MASK;
		return K_STATUS_OK;
	}

	paddr_t l3_phys = l2[idx.l2_index] & PAGE_MASK;
	pte_t *l3 = l2v(l3_phys);

	/* Level 3 Evaluation (4KB Base Page Check) */
	if (l3[idx.l3_index] & ARM_PAGE_DESCRIPTOR) {
		paddr_t phys_base = l3[idx.l3_index] & PAGE_MASK;
		*phys_out = phys_base + idx.offset;
		*flags_out = l3[idx.l3_index] & PTE_FLAGS_MASK;
		return K_STATUS_OK;
	}

	return K_STATUS_NOT_MAPPED;
}

/* Flushes the entire TLB cache hardware tracking table across all active SMP cores */
k_status_t pt_flush(void)
{
	__asm__ volatile("tlbi vmalle1is");
	__asm__ volatile("dsb ish");
	__asm__ volatile("isb");
	return K_STATUS_OK;
}

/* Evicts range explicit address context structures out of the TLB pipeline */
k_status_t pt_invalidate(vaddr_t virt, uint64_t pg_count, page_size_t pg_size)
{
	for (size_t i = 0; i < pg_count; ++i) {
		vaddr_t target = virt + (i * pg_size);
		vaddr_t val = target >> 12;

		__asm__ volatile("tlbi vale1is, %0" : : "r"(val));
	}

	__asm__ volatile("dsb ish");
	__asm__ volatile("isb");
	return K_STATUS_OK;
}

/* Configures MAIR profile attributes inside the host register file vector */
k_status_t pt_set_mair(uint64_t mair)
{
	__asm__ volatile("msr mair_el1, %0" : : "r"(mair));
	__asm__ volatile("isb");
	return K_STATUS_OK;
}
