#include "page_table.h"
#include <stddef.h>

#include "../modules.h"
#include "../../include/api/pmm.h"

#include "../utils.h"

EXTERN_IMPORT_INTERFACE(pmm, pmm);

#define FLAG_MASK 0xFFFF000000000FFFULL
#define ADDR_MASK 0x0000FFFFFFFFF000ULL
#define HHDM_OFFSET 0xFFFF800000000000ULL 

static inline void* p2v(phys_addr_t phys) {
    // Adding the offset "shifts" the address into the kernel's accessible window
    return (void*)(phys + HHDM_OFFSET);
}

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

static void pte_recursive_free(phys_addr_t table_phys, int level) {
    uint64_t *table = (uint64_t *)p2v(table_phys); // Use your new Direct Map!

    // Only recurse if we aren't at the bottom level (L3)
    if (level < 3) {
        for (int i = 0; i < 512; i++) {
            uint64_t entry = table[i];
            
            // Check if it's a valid table descriptor (Bit 1 must be 1 for L0-L2)
            if ((entry & 1) && (entry & ARM_TABLE_DESCRIPTOR)) {
                phys_addr_t child_phys = entry & PAGE_MASK;
                pte_recursive_free(child_phys, level + 1);
            }
        }
    }

    // After children are gone, free the current table
    pmm.free_page(table_phys);
}

k_status_t table_free(phys_addr_t root)
{
    if (!root) return K_STATUS_BAD_PHYS_ADDR;

    // Start the recursive destruction from Level 0
    pte_recursive_free(root, 0);
    
    return K_STATUS_OK;
}

static k_status_t pte_deep_copy(phys_addr_t src_table_phys, phys_addr_t *dst_table_phys, int level) {
    k_status_t status;
    phys_addr_t new_page_phys;

    status = pmm.alloc_page(&new_page_phys);
    if (k_error(status)) return status;

    // FIX: Convert physical addresses to virtual pointers using p2v
    uint64_t *src = (uint64_t *)p2v(src_table_phys);
    uint64_t *dst = (uint64_t *)p2v(new_page_phys);

    memset(dst, 0, 4096);

    for (int i = 0; i < 512; i++) {
        if (!(src[i] & 1)) continue;

        if ((src[i] & ARM_TABLE_DESCRIPTOR) && level < 3) {
            phys_addr_t child_dst_phys;
            // Recurse using the physical address from the entry
            status = pte_deep_copy(src[i] & PAGE_MASK, &child_dst_phys, level + 1);
            if (k_error(status)) return status;

            dst[i] = child_dst_phys | (src[i] & ~PAGE_MASK);
        } else {
            dst[i] = src[i];
        }
    }

    *dst_table_phys = new_page_phys;
    return K_STATUS_OK;
}

k_status_t table_copy(phys_addr_t src_root, phys_addr_t *dest_root) {
    if (!src_root || !dest_root) return K_STATUS_BAD_PHYS_ADDR;

    // Start the recursive walk from Level 0 (P0)
    return pte_deep_copy(src_root, dest_root, 0);
}

k_status_t set_user_context(phys_addr_t root, uint16_t asid)
{
    // AArch64 ASID lives in bits [63:48] of TTBR0_EL1
    uint64_t ttbr = ((uint64_t)asid << 48) | (root & PAGE_MASK);
    __asm__ volatile("msr ttbr0_el1, %0" : : "r"(ttbr));
    __asm__ volatile("isb");
    return K_STATUS_OK;
}

k_status_t set_kernel_context(phys_addr_t root, uint16_t asid)
{
    // AArch64 ASID lives in bits [63:48] of TTBR0_EL1
    uint64_t ttbr = ((uint64_t)asid << 48) | (root & PAGE_MASK);
    __asm__ volatile("msr ttbr1_el1, %0" : : "r"(ttbr));
    __asm__ volatile("isb");
    return K_STATUS_OK;
}

k_status_t map(phys_addr_t root, virt_addr_t v, phys_addr_t p, size_t pc, page_size_t ps, mmu_flags_t f)
{
    if (!root) return K_STATUS_BAD_PHYS_ADDR;

    // 1. Get a virtual pointer to the root table once.
    uint64_t* p0 = (uint64_t*)p2v(root);

    for (size_t i = 0; i < pc; i++) {
        virt_addr_t curr_vaddr = v + (i * ps);
        phys_addr_t curr_phys  = p + (i * ps);
        page_table_indices_t idx = extract_indices(curr_vaddr);

        // --- Level 0 -> Level 1 ---
        uint64_t* p1;
        uint64_t entry0 = p0[idx.p0_index];
        
        if (!(entry0 & 1)) { // If not valid
            phys_addr_t p1_phys;
            if (k_error(pmm.alloc_page(&p1_phys))) return K_STATUS_OUT_OF_MEMORY;
            
            p1 = (uint64_t*)p2v(p1_phys);
            memset(p1, 0, 4096);
            p0[idx.p0_index] = p1_phys | ARM_TABLE_DESCRIPTOR;
        } else {
            p1 = (uint64_t*)p2v(entry0 & PAGE_MASK);
        }

        if (ps == PS_1GB) {
            p1[idx.p1_index] = curr_phys | f; 
            continue;
        }

        // --- Level 1 -> Level 2 ---
        uint64_t* p2;
        uint64_t entry1 = p1[idx.p1_index];
        
        if (!(entry1 & 1)) {
            phys_addr_t p2_phys;
            if (k_error(pmm.alloc_page(&p2_phys))) return K_STATUS_OUT_OF_MEMORY;
            
            p2 = (uint64_t*)p2v(p2_phys);
            memset(p2, 0, 4096);
            p1[idx.p1_index] = p2_phys | ARM_TABLE_DESCRIPTOR;
        } else {
            p2 = (uint64_t*)p2v(entry1 & PAGE_MASK);
        }

        if (ps == PS_2MB) {
            p2[idx.p2_index] = curr_phys | f;
            continue;
        }

        // --- Level 2 -> Level 3 ---
        uint64_t* p3;
        uint64_t entry2 = p2[idx.p2_index];
        
        if (!(entry2 & 1)) {
            phys_addr_t p3_phys;
            if (k_error(pmm.alloc_page(&p3_phys))) return K_STATUS_OUT_OF_MEMORY;
            
            p3 = (uint64_t*)p2v(p3_phys);
            memset(p3, 0, 4096);
            p2[idx.p2_index] = p3_phys | ARM_TABLE_DESCRIPTOR;
        } else {
            p3 = (uint64_t*)p2v(entry2 & PAGE_MASK);
        }

        // --- Level 3 Page ---
        p3[idx.p3_index] = curr_phys | f | ARM_PAGE_DESCRIPTOR;
    }

    return tlb_invalidate(v, pc, ps);
}

// Helper to check if a 4KB table page is entirely empty
static bool is_table_empty(phys_addr_t table_phys) {
    // We MUST use p2v to read the table memory
    uint64_t *table = (uint64_t *)p2v(table_phys); 
    for (int i = 0; i < 512; i++) {
        if (table[i] != 0) return false;
    }
    return true;
}

k_status_t unmap(phys_addr_t root, virt_addr_t v, size_t pc, page_size_t ps)
{
    if (!root) return K_STATUS_BAD_PHYS_ADDR;

    // Convert physical root to virtual pointer once
    uint64_t* p0 = (uint64_t*)p2v(root);

    for (unsigned long i = 0; i < pc; i++) {
        unsigned long curr_vaddr = v + i * ps;
        page_table_indices_t idx = extract_indices(curr_vaddr);

        // --- L0 -> L1 ---
        if (!(p0[idx.p0_index] & 1)) continue;
        phys_addr_t p1_phys = p0[idx.p0_index] & PAGE_MASK;
        uint64_t* p1 = (uint64_t*)p2v(p1_phys);

        if (ps == PS_1GB) {
            p1[idx.p1_index] = 0;
        } else {
            // --- L1 -> L2 ---
            if (!(p1[idx.p1_index] & 1)) continue;
            phys_addr_t p2_phys = p1[idx.p1_index] & PAGE_MASK;
            uint64_t* p2 = (uint64_t*)p2v(p2_phys);

            if (ps == PS_2MB) {
                p2[idx.p2_index] = 0;
            } else {
                // --- L2 -> L3 ---
                if (!(p2[idx.p2_index] & 1)) continue;
                phys_addr_t p3_phys = p2[idx.p2_index] & PAGE_MASK;
                uint64_t* p3 = (uint64_t*)p2v(p3_phys);

                if (ps == PS_4KB) {
                    p3[idx.p3_index] = 0;
                }

                // PRUNING: If P3 is now empty, free it
                if (is_table_empty(p3_phys)) {
                    pmm.free_page(p3_phys);
                    p2[idx.p2_index] = 0;
                }
            }

            // PRUNING: If P2 is now empty, free it
            if (is_table_empty(p2_phys)) {
                pmm.free_page(p2_phys);
                p1[idx.p1_index] = 0;
            }
        }

        // PRUNING: If P1 is now empty, free it
        if (is_table_empty(p1_phys)) {
            pmm.free_page(p1_phys);
            p0[idx.p0_index] = 0;
        }
    }

    return tlb_invalidate(v, pc, ps);
}

k_status_t protect(phys_addr_t root, virt_addr_t v, size_t pc, page_size_t ps, mmu_flags_t f)
{
    if (!root) return K_STATUS_BAD_PHYS_ADDR;

    // Convert physical root to virtual pointer
    uint64_t* p0 = (uint64_t*)p2v(root);

    for (unsigned long i = 0; i < pc; i++) {
        unsigned long curr_vaddr = v + i * ps;
        page_table_indices_t idx = extract_indices(curr_vaddr);

        // --- Level 0 -> Level 1 ---
        if (!(p0[idx.p0_index] & 1)) return K_STATUS_NOT_MAPPED;
        uint64_t* p1 = (uint64_t*)p2v(p0[idx.p0_index] & PAGE_MASK);

        // 1GB Section Protection
        if (ps == PS_1GB) {
            if (!(p1[idx.p1_index] & 1)) return K_STATUS_NOT_MAPPED;
            // Preserve the address bits, replace flags
            uint64_t phys_addr = p1[idx.p1_index] & ADDR_MASK;
            p1[idx.p1_index] = phys_addr | f; 
            continue;
        }

        // --- Level 1 -> Level 2 ---
        if (!(p1[idx.p1_index] & 1)) return K_STATUS_NOT_MAPPED;
        uint64_t* p2 = (uint64_t*)p2v(p1[idx.p1_index] & PAGE_MASK);

        // 2MB Section Protection
        if (ps == PS_2MB) {
            if (!(p2[idx.p2_index] & 1)) return K_STATUS_NOT_MAPPED;
            uint64_t phys_addr = p2[idx.p2_index] & ADDR_MASK;
            p2[idx.p2_index] = phys_addr | f;
            continue;
        }

        // --- Level 2 -> Level 3 ---
        if (!(p2[idx.p2_index] & 1)) return K_STATUS_NOT_MAPPED;
        uint64_t* p3 = (uint64_t*)p2v(p2[idx.p2_index] & PAGE_MASK);

        // 4KB Page Protection
        if (ps == PS_4KB) {
            if (!(p3[idx.p3_index] & 1)) return K_STATUS_NOT_MAPPED;
            uint64_t phys_addr = p3[idx.p3_index] & ADDR_MASK;
            // Force ARM_PAGE_DESCRIPTOR (Bit 1) for L3 entries
            p3[idx.p3_index] = phys_addr | f | ARM_PAGE_DESCRIPTOR;
        }
    }

    // Must invalidate TLB or the CPU will keep using the old cached permissions
    return tlb_invalidate(v, pc, ps);
}

k_status_t translate(phys_addr_t root, virt_addr_t v, phys_addr_t *out_p, mmu_flags_t *out_f)
{
    if (!root || !out_p || !out_f) return K_STATUS_BAD_PHYS_ADDR;
    
    page_table_indices_t idx = extract_indices(v);
    
    // --- Walk P0 (Level 0) ---
    // root is physical, we need a virtual pointer to read it
    uint64_t* p0 = (uint64_t*)p2v(root); 
    
    if (!(p0[idx.p0_index] & 1)) return K_STATUS_NOT_MAPPED;
    
    // --- Walk P1 (Level 1) ---
    phys_addr_t p1_phys = p0[idx.p0_index] & PAGE_MASK;
    uint64_t* p1 = (uint64_t*)p2v(p1_phys);
    
    // Level 1 (1GB Check) - A Block descriptor has Bit 1 as 0
    if (!(p1[idx.p1_index] & (1ULL << 1))) { 
        if (!(p1[idx.p1_index] & 1)) return K_STATUS_NOT_MAPPED;

        phys_addr_t phys_base = p1[idx.p1_index] & ~0x3FFFFFFF;
        *out_p = phys_base + (v & 0x3FFFFFFF);
        *out_f = p1[idx.p1_index] & FLAG_MASK;
        return K_STATUS_OK;
    }
    
    // --- Walk P2 (Level 2) ---
    phys_addr_t p2_phys = p1[idx.p1_index] & PAGE_MASK;
    uint64_t* p2 = (uint64_t*)p2v(p2_phys);
    
    // Level 2 (2MB Check)
    if (!(p2[idx.p2_index] & (1ULL << 1))) {
        if (!(p2[idx.p2_index] & 1)) return K_STATUS_NOT_MAPPED;

        phys_addr_t phys_base = p2[idx.p2_index] & ~0x1FFFFF;
        *out_p = phys_base + (v & 0x1FFFFF);
        *out_f = p2[idx.p2_index] & FLAG_MASK;
        return K_STATUS_OK;
    }
    
    // --- Walk P3 (Level 3) ---
    phys_addr_t p3_phys = p2[idx.p2_index] & PAGE_MASK;
    uint64_t* p3 = (uint64_t*)p2v(p3_phys);
    
    // Level 3 (4KB Check)
    if (p3[idx.p3_index] & ARM_PAGE_DESCRIPTOR) {
        phys_addr_t phys_base = p3[idx.p3_index] & PAGE_MASK;
        *out_p = phys_base + (v & 0xFFF); // or idx.offset
        *out_f = p3[idx.p3_index] & FLAG_MASK;
        return K_STATUS_OK;
    }
    
    return K_STATUS_NOT_MAPPED;
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
        virt_addr_t target = v + (i * ps);

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

