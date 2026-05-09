#include "page_table.h"
#include <stddef.h>

#include "../modules.h"
#include "../../include/api/pmm.h"

#include "../utils.h"

EXTERN_IMPORT_INTERFACE(pmm, pmm);

#define to_array(var) ((unsigned long *)(var))

static page_table_indices_t extract_indices(virt_addr_t virtual_address) {
  page_table_indices_t indices;
  indices.offset = virtual_address & 0xFFF;     /* Page offset (bits 0-11) */
  indices.p3_index = P3_INDEX(virtual_address); /* Page Table index */
  indices.p2_index = P2_INDEX(virtual_address); /* Page Directory index */
  indices.p1_index = P1_INDEX(virtual_address); /* PDPT index */
  indices.p0_index = P0_INDEX(virtual_address); /* PML4 index */
  return indices;
}

k_status_t table_alloc(phys_addr_t *out_root)
{
    return pmm.alloc_page(out_root);
}

k_status_t table_free(phys_addr_t root)
{
    return pmm.free_page(root);
}

k_status_t table_copy(phys_addr_t src_root, phys_addr_t *dest_root)
{

}

k_status_t set_user_context(phys_addr_t root, uint16_t acid)
{
    __asm__ volatile("msr ttbr0_el1, %0" : : "r"(root));
    return K_STATUS_OK;
}

k_status_t set_kernel_context(phys_addr_t root, uint16_t acid)
{
    __asm__ volatile("msr ttbr1_el1, %0" : : "r"(root));
    return K_STATUS_OK;
}

k_status_t map(phys_addr_t root, virt_addr_t v, phys_addr_t p, size_t pc, page_size_t ps, mmu_flags_t f)
{
    if (!root) {
        return K_STATUS_BAD_PHYS_ADDR;
    }

    k_status_t status;

    phys_addr_t* p0 = *(phys_addr_t**)root;

    for (unsigned long i = 0; i < pc; i++) {
        unsigned long curr_vaddr = v + i * ps;
        unsigned long curr_phys = p + i * ps;
        page_table_indices_t idx = extract_indices(curr_vaddr);

        // Get or create P1 table
        phys_addr_t* p1;
        if (!(p0[idx.p0_index] & ARM_TABLE_DESCRIPTOR)) {

            status = pmm.alloc_page((phys_addr_t *)&p1);
            if (k_error(status))
                return status;
            memset(p1, 0, 4096);
            p0[idx.p0_index] = (phys_addr_t)p1 | ARM_TABLE_DESCRIPTOR;
        } else {
            p1 = (phys_addr_t*)(p0[idx.p0_index] & PAGE_MASK);
        }

        // 1GB pages (order 2)
        if (ps == PS_1GB) {
            p1[idx.p1_index] = curr_phys | ARM_KERNEL_FLAGS;
            continue;
        }

        // Get or create P2 table
        phys_addr_t *p2;
        if (!(p1[idx.p1_index] & ARM_TABLE_DESCRIPTOR)) {
            status = pmm.alloc_page((phys_addr_t *)&p2);
            if (k_error(status)) return status;
            memset(p2, 0, 4096);
            p1[idx.p1_index] = (phys_addr_t)p2 | ARM_TABLE_DESCRIPTOR;
        } else {
            p2 = (phys_addr_t *)(p1[idx.p1_index] & PAGE_MASK);
        }

        // 2MB pages (order 1)
        if (ps == PS_2MB) {
            p2[idx.p2_index] = curr_phys | ARM_KERNEL_FLAGS;
            continue;
        }

        // Get or create P3 table for 4KB pages (order 0)
        phys_addr_t *p3;
        if (!(p2[idx.p2_index] & ARM_TABLE_DESCRIPTOR)) {
            status = pmm.alloc_page((phys_addr_t*)&p3);
            if (k_error(status)) return status;
            memset(p3, 0, 4096);
            p2[idx.p2_index] = (phys_addr_t)p3 | ARM_TABLE_DESCRIPTOR;
        } else {
            p3 = (phys_addr_t *)(p2[idx.p2_index] & PAGE_MASK);
        }

        // 4KB pages (order 0)
        if (ps == PS_4KB) {
            p3[idx.p3_index] = curr_phys | ARM_4KB_PAGE_FLAGS;
            continue;
        }
    }

    // Make sure to invalidate the pages that were changed
    tlb_invalidate(v, pc, ps);

    return K_STATUS_OK;
}

k_status_t unmap(phys_addr_t root, virt_addr_t v, size_t pc, page_size_t ps)
{
    // TODO change this to unmap instead of map (set to 0)
    if (!root) {
        return K_STATUS_BAD_PHYS_ADDR;
    }

    k_status_t status;

    phys_addr_t* p0 = *(phys_addr_t**)root;

    for (unsigned long i = 0; i < pc; i++) {
        unsigned long curr_vaddr = v + i * ps;
        unsigned long curr_phys = 0 + i * ps;
        page_table_indices_t idx = extract_indices(curr_vaddr);

        // Get or create P1 table
        phys_addr_t* p1;
        if (!(p0[idx.p0_index] & ARM_TABLE_DESCRIPTOR)) {

            status = pmm.alloc_page((phys_addr_t *)&p1);
            if (k_error(status))
                return status;
            memset(p1, 0, 4096);
            p0[idx.p0_index] = (phys_addr_t)p1 | ARM_TABLE_DESCRIPTOR;
        } else {
            p1 = (phys_addr_t*)(p0[idx.p0_index] & PAGE_MASK);
        }

        // 1GB pages (order 2)
        if (ps == PS_1GB) {
            p1[idx.p1_index] = curr_phys | ARM_KERNEL_FLAGS;
            continue;
        }

        // Get or create P2 table
        phys_addr_t *p2;
        if (!(p1[idx.p1_index] & ARM_TABLE_DESCRIPTOR)) {
            status = pmm.alloc_page((phys_addr_t *)&p2);
            if (k_error(status)) return status;
            memset(p2, 0, 4096);
            p1[idx.p1_index] = (phys_addr_t)p2 | ARM_TABLE_DESCRIPTOR;
        } else {
            p2 = (phys_addr_t *)(p1[idx.p1_index] & PAGE_MASK);
        }

        // 2MB pages (order 1)
        if (ps == PS_2MB) {
            p2[idx.p2_index] = curr_phys | ARM_KERNEL_FLAGS;
            continue;
        }

        // Get or create P3 table for 4KB pages (order 0)
        phys_addr_t *p3;
        if (!(p2[idx.p2_index] & ARM_TABLE_DESCRIPTOR)) {
            status = pmm.alloc_page((phys_addr_t*)&p3);
            if (k_error(status)) return status;
            memset(p3, 0, 4096);
            p2[idx.p2_index] = (phys_addr_t)p3 | ARM_TABLE_DESCRIPTOR;
        } else {
            p3 = (phys_addr_t *)(p2[idx.p2_index] & PAGE_MASK);
        }

        // 4KB pages (order 0)
        if (ps == PS_4KB) {
            p3[idx.p3_index] = curr_phys | ARM_4KB_PAGE_FLAGS;
            continue;
        }
    }

    // Make sure to invalidate the pages that were changed
    tlb_invalidate(v, pc, ps);

    return K_STATUS_OK;
}

k_status_t protect(phys_addr_t root, virt_addr_t v, size_t pc, page_size_t ps, mmu_flags_t f)
{
    // TODO change this to protect instead of map
    if (!root) {
        return K_STATUS_BAD_PHYS_ADDR;
    }

    k_status_t status;

    phys_addr_t* p0 = *(phys_addr_t**)root;

    for (unsigned long i = 0; i < pc; i++) {
        unsigned long curr_vaddr = v + i * ps;
        unsigned long curr_phys = 0 + i * ps;
        page_table_indices_t idx = extract_indices(curr_vaddr);

        // Get or create P1 table
        phys_addr_t* p1;
        if (!(p0[idx.p0_index] & ARM_TABLE_DESCRIPTOR)) {

            status = pmm.alloc_page((phys_addr_t *)&p1);
            if (k_error(status))
                return status;
            memset(p1, 0, 4096);
            p0[idx.p0_index] = (phys_addr_t)p1 | ARM_TABLE_DESCRIPTOR;
        } else {
            p1 = (phys_addr_t*)(p0[idx.p0_index] & PAGE_MASK);
        }

        // 1GB pages (order 2)
        if (ps == PS_1GB) {
            p1[idx.p1_index] = curr_phys | ARM_KERNEL_FLAGS;
            continue;
        }

        // Get or create P2 table
        phys_addr_t *p2;
        if (!(p1[idx.p1_index] & ARM_TABLE_DESCRIPTOR)) {
            status = pmm.alloc_page((phys_addr_t *)&p2);
            if (k_error(status)) return status;
            memset(p2, 0, 4096);
            p1[idx.p1_index] = (phys_addr_t)p2 | ARM_TABLE_DESCRIPTOR;
        } else {
            p2 = (phys_addr_t *)(p1[idx.p1_index] & PAGE_MASK);
        }

        // 2MB pages (order 1)
        if (ps == PS_2MB) {
            p2[idx.p2_index] = curr_phys | ARM_KERNEL_FLAGS;
            continue;
        }

        // Get or create P3 table for 4KB pages (order 0)
        phys_addr_t *p3;
        if (!(p2[idx.p2_index] & ARM_TABLE_DESCRIPTOR)) {
            status = pmm.alloc_page((phys_addr_t*)&p3);
            if (k_error(status)) return status;
            memset(p3, 0, 4096);
            p2[idx.p2_index] = (phys_addr_t)p3 | ARM_TABLE_DESCRIPTOR;
        } else {
            p3 = (phys_addr_t *)(p2[idx.p2_index] & PAGE_MASK);
        }

        // 4KB pages (order 0)
        if (ps == PS_4KB) {
            p3[idx.p3_index] = curr_phys | ARM_4KB_PAGE_FLAGS;
            continue;
        }
    }

    // Make sure to invalidate the pages that were changed
    tlb_invalidate(v, pc, ps);

    return K_STATUS_OK;
}

k_status_t translate(phys_addr_t root, virt_addr_t v, phys_addr_t *out_p, mmu_flags_t *out_f)
{
    if (!root) return K_STATUS_BAD_PHYS_ADDR;
    
    page_table_indices_t idx = extract_indices(v);
    
    // Walk P0 (Level 0)
    unsigned long* p0 = (unsigned long*)root;
    if (!(p0[idx.p0_index] & ARM_TABLE_DESCRIPTOR)) {
        return K_STATUS_BAD_VIRT_ADDR; // No mapping at P0 level
    }
    unsigned long* p1 = (unsigned long*)(p0[idx.p0_index] & PAGE_MASK);
    
    // Check if this is a 1GB block (order 2)
    if (p1[idx.p1_index] & ARM_BLOCK_DESCRIPTOR) {
        // 1GB block - extract physical address
        phys_addr_t phys_base = p1[idx.p1_index] & ~0x3FFFFFFF; // Clear flags and mask to 1GB
        mmu_flags_t flags = p1[idx.p1_index] & 0xFFFF000000000FFFULL;
        *out_p = phys_base + (v & 0x3FFFFFFF);
        *out_f = flags;
        return K_STATUS_OK;
    }
    
    // Walk P1 (Level 1) - must be a table descriptor for smaller pages
    if (!(p1[idx.p1_index] & ARM_TABLE_DESCRIPTOR)) {
        return K_STATUS_BAD_VIRT_ADDR; // No mapping at P1 level
    }
    unsigned long* p2 = (unsigned long*)(p1[idx.p1_index] & PAGE_MASK);
    
    // Check if this is a 2MB block (order 1)
    if (p2[idx.p2_index] & ARM_BLOCK_DESCRIPTOR) {
        // 2MB block - extract physical address
        phys_addr_t phys_base = p2[idx.p2_index] & ~0x1FFFFF; // Clear flags and mask to 2MB
        mmu_flags_t flags = p1[idx.p1_index] & 0xFFFF000000000FFFULL;
        *out_p = phys_base + (v & 0x1FFFFF);
        *out_f = flags;
        return K_STATUS_OK;
    }
    
    // Walk P2 (Level 2) - must be a table descriptor for 4KB pages
    if (!(p2[idx.p2_index] & ARM_TABLE_DESCRIPTOR)) {
        return K_STATUS_BAD_VIRT_ADDR; // No mapping at P2 level
    }
    unsigned long* p3 = (unsigned long*)(p2[idx.p2_index] & PAGE_MASK);
    
    // Check if this is a 4KB page (order 0)
    if (p3[idx.p3_index] & ARM_PAGE_DESCRIPTOR) {
        // 4KB page - extract physical address
        phys_addr_t phys_base = p3[idx.p3_index] & PAGE_MASK; // Clear flags
        mmu_flags_t flags = p1[idx.p1_index] & 0xFFFF000000000FFFULL;
        *out_p =  phys_base + idx.offset;
        *out_f = flags;
        return K_STATUS_OK;
    }
    
    return K_STATUS_BAD_VIRT_ADDR; // No valid mapping found
}

k_status_t flush_tlb(void)
{
    __asm__ volatile ("tlbi vmalle1is");
    __asm__ volatile ("dsb ish");
    __asm__ volatile ("isb");

    return K_STATUS_OK;
}

k_status_t tlb_invalidate(virt_addr_t v, size_t pc, page_size_t ps)
{
    for (size_t i = 0; i < pc; ++i)
    {
        virt_addr_t target = v + (8 * ps);

        uint64_t val = target >> 12;

        __asm__ volatile ("tlbi vale1is, %0" : : "r" (val));
    }

    __asm__ volatile ("dsb ish");
    __asm__ volatile ("isb");

    return K_STATUS_OK;
}

k_status_t set_mair(uint64_t mair_value)
{
    __asm__ volatile ("msr mair_el1, %0" : : "r"(mair_value));
    __asm__ volatile ("isb");

    return K_STATUS_OK;
}

