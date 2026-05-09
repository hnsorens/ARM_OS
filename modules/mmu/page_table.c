#include "page_table.h"
#include <stddef.h>

#include "../modules.h"
#include "../../include/api/pmm.h"

#include "../utils.h"

EXTERN_IMPORT_INTERFACE(pmm, pmm);

static page_table_indices_t extract_indices(virt_addr_t virtual_address) {
  page_table_indices_t indices;
  indices.offset = virtual_address & 0xFFF;     /* Page offset (bits 0-11) */
  indices.p3_index = P3_INDEX(virtual_address); /* Page Table index */
  indices.p2_index = P2_INDEX(virtual_address); /* Page Directory index */
  indices.p1_index = P1_INDEX(virtual_address); /* PDPT index */
  indices.p0_index = P0_INDEX(virtual_address); /* PML4 index */
  return indices;
}

static unsigned long page_order_size(unsigned long order)
{
  switch (order) {
    case 0:
      return 4096;
    case 1:
      return 4096 * 512;
    case 2:
      return 4096 * 512 * 512;
  }
  return 0;
}

k_status_t table_alloc(phys_addr_t *out_root)
{

}

k_status_t table_free(phys_addr_t root)
{

}

k_status_t table_copy(phys_addr_t src_root, phys_addr_t *dest_root)
{

}

k_status_t activate(phys_addr_t root, uint16_t acid)
{

}

k_status_t map(phys_addr_t root, virt_addr_t v, phys_addr_t p, size_t pc, page_size_t ps, mmu_flags_t f)
{
    if (!root) {
        return K_STATUS_BAD_PHYS_ADDR;
    }

    void** root_ptr = (void**)root;
    
    unsigned long* p0 = *root_ptr;

    for (unsigned long i = 0; i < pc; i++) {
        unsigned long curr_vaddr = v + i * page_order_size(ps);
        unsigned long curr_phys = p + i * page_order_size(ps);
        page_table_indices_t idx = extract_indices(curr_vaddr);

        // Get or create P1 table
        unsigned long* p1;
        if (!(p0[idx.p0_index] & ARM_TABLE_DESCRIPTOR)) {
            p1 = (unsigned long*)pmm_alloc_phys(0);
            if (!p1) return;
            kmemset(p1, 0, 4096);
            p0[idx.p0_index] = (unsigned long)p1 | ARM_TABLE_DESCRIPTOR;
        } else {
            p1 = (unsigned long*)(p0[idx.p0_index] & PAGE_MASK);
        }

        // 1GB pages (order 2)
        if (page_order == 2) {
            p1[idx.p1_index] = curr_phys | ARM_KERNEL_FLAGS;
            continue;
        }

        // Get or create P2 table
        unsigned long* p2;
        if (!(p1[idx.p1_index] & ARM_TABLE_DESCRIPTOR)) {
            p2 = (unsigned long*)pmm_alloc_phys(0);
            if (!p2) return;
            kmemset(p2, 0, 4096);
            p1[idx.p1_index] = (unsigned long)p2 | ARM_TABLE_DESCRIPTOR;
        } else {
            p2 = (unsigned long*)(p1[idx.p1_index] & PAGE_MASK);
        }

        // 2MB pages (order 1)
        if (page_order == 1) {
            p2[idx.p2_index] = curr_phys | ARM_KERNEL_FLAGS;
            continue;
        }

        // Get or create P3 table for 4KB pages (order 0)
        unsigned long* p3;
        if (!(p2[idx.p2_index] & ARM_TABLE_DESCRIPTOR)) {
            p3 = (unsigned long*)pmm_alloc_phys(0);
            if (!p3) return;
            kmemset(p3, 0, 4096);
            p2[idx.p2_index] = (unsigned long)p3 | ARM_TABLE_DESCRIPTOR;
        } else {
            p3 = (unsigned long*)(p2[idx.p2_index] & PAGE_MASK);
        }

        // 4KB pages (order 0)
        if (page_order == 0) {
            p3[idx.p3_index] = curr_phys | ARM_4KB_PAGE_FLAGS;
            continue;
        }
    }

}

k_status_t unmap(phys_addr_t root, virt_addr_t v, size_t pc, page_size_t ps)
{

}

k_status_t protect(phys_addr_t root, virt_addr_t v, size_t pc, page_size_t ps, mmu_flags_t f)
{

}

k_status_t translate(phys_addr_t root, virt_addr_t v, phys_addr_t *out_p, mmu_flags_t *out_f)
{

}

k_status_t flush_tlb(void)
{

}

k_status_t tlb_invalidate(virt_addr_t v, size_t pc, page_size_t ps)
{

}

k_status_t set_mair(uint8_t index, uint8_t addr)
{

}



void page_enable(page_table_t page_table)
{
    // Configure Memory Attributes
    unsigned long mair = MAIR_ATTR(MAIR_NORMAL_WB, MAIR_IDX_NORMAL) |
                    MAIR_ATTR(MAIR_DEVICE_nGnRE, MAIR_IDX_DEVICE);
    __asm__ volatile("msr mair_el1, %0" : : "r"(mair));

    
    // Configure Translation Control
    unsigned long tcr = (TCR_TBI_DISABLE << TCR_TBI_SHIFT) |
                   (TCR_IPS_40BIT << TCR_IPS_SHIFT) |
                   (TCR_TG_4KB << TCR_TG1_SHIFT) |
                   (TCR_SH_INNER << TCR_SH1_SHIFT) |
                   (TCR_RGN_WB << TCR_ORGN1_SHIFT) |
                   (TCR_RGN_WB << TCR_IRGN1_SHIFT) |
                   (TCR_TG_4KB << TCR_TG0_SHIFT) |
                   (TCR_SH_INNER << TCR_SH0_SHIFT) |
                   (TCR_RGN_WB << TCR_ORGN0_SHIFT) |
                   (TCR_RGN_WB << TCR_IRGN0_SHIFT) |
                   (TCR_T0SZ_48BIT << TCR_T1SZ_SHIFT) |
                   (TCR_T0SZ_48BIT << TCR_T0SZ_SHIFT);
    __asm__ volatile("msr tcr_el1, %0" : : "r"(tcr));

    // Set Page Table Base
    __asm__ volatile("msr ttbr0_el1, %0" : : "r"((unsigned long)page_table));

    // Invalidate TLB
    __asm__ volatile("dsb sy");
    __asm__ volatile("tlbi vmalle1");
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");

    // Enable MMU
    unsigned long sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= SCTLR_M_ENABLE | SCTLR_C_ENABLE | SCTLR_I_ENABLE;
    __asm__ volatile("msr sctlr_el1, %0" : : "r"(sctlr));
    __asm__ volatile("isb");


}

void pages_map(page_table_t* page_table, vaddr_t virtual_address, 
               paddr_t phys_addr, unsigned long page_order, unsigned long page_count)
{   
    if (!page_table) {
        return;
    }

    // Initialize root page table if it doesn't exist
    if (!*page_table) {
        *page_table = (unsigned long*)pmm_alloc_phys(0);
        if (!*page_table)
            return;
        kmemset((void*)(*page_table), 0, 4096);
    }
    
    unsigned long* p0 = *page_table;

    for (unsigned long i = 0; i < page_count; i++) {
        unsigned long curr_vaddr = virtual_address + i * page_order_size(page_order);
        unsigned long curr_phys = phys_addr + i * page_order_size(page_order);
        page_table_indices_t idx = extract_indices(curr_vaddr);

        // Get or create P1 table
        unsigned long* p1;
        if (!(p0[idx.p0_index] & ARM_TABLE_DESCRIPTOR)) {
            p1 = (unsigned long*)pmm_alloc_phys(0);
            if (!p1) return;
            kmemset(p1, 0, 4096);
            p0[idx.p0_index] = (unsigned long)p1 | ARM_TABLE_DESCRIPTOR;
        } else {
            p1 = (unsigned long*)(p0[idx.p0_index] & PAGE_MASK);
        }

        // 1GB pages (order 2)
        if (page_order == 2) {
            p1[idx.p1_index] = curr_phys | ARM_KERNEL_FLAGS;
            continue;
        }

        // Get or create P2 table
        unsigned long* p2;
        if (!(p1[idx.p1_index] & ARM_TABLE_DESCRIPTOR)) {
            p2 = (unsigned long*)pmm_alloc_phys(0);
            if (!p2) return;
            kmemset(p2, 0, 4096);
            p1[idx.p1_index] = (unsigned long)p2 | ARM_TABLE_DESCRIPTOR;
        } else {
            p2 = (unsigned long*)(p1[idx.p1_index] & PAGE_MASK);
        }

        // 2MB pages (order 1)
        if (page_order == 1) {
            p2[idx.p2_index] = curr_phys | ARM_KERNEL_FLAGS;
            continue;
        }

        // Get or create P3 table for 4KB pages (order 0)
        unsigned long* p3;
        if (!(p2[idx.p2_index] & ARM_TABLE_DESCRIPTOR)) {
            p3 = (unsigned long*)pmm_alloc_phys(0);
            if (!p3) return;
            kmemset(p3, 0, 4096);
            p2[idx.p2_index] = (unsigned long)p3 | ARM_TABLE_DESCRIPTOR;
        } else {
            p3 = (unsigned long*)(p2[idx.p2_index] & PAGE_MASK);
        }

        // 4KB pages (order 0)
        if (page_order == 0) {
            p3[idx.p3_index] = curr_phys | ARM_4KB_PAGE_FLAGS;
            continue;
        }
    }
}

page_table_t pages_create_identity_page_table(size_t total_memory)
{
  // Allocate L0 table (512GB blocks)
    page_table_t page_table = pmm_alloc_phys(0);
    if (!page_table) {
      return 0;
    }
    kmemset(page_table, 0, 4096);

    // Calculate how many 512GB blocks we need
    unsigned long blocks_needed = (total_memory + 0x7FFFFFFFFFULL) / 0x8000000000ULL;
    if (blocks_needed == 0) blocks_needed = 1; // At least one block

    // For identity mapping, create 1GB block mappings in L1 tables
    for (unsigned long block = 0; block < blocks_needed; block++) {
        // Allocate L1 table for this 512GB block
        unsigned long* l1_table = pmm_alloc_phys(0);
        if (!l1_table) {
          return 0;
        }
        kmemset(l1_table, 0, 4096);
        
        // Set L0 entry to point to L1 table
        page_table[block] = (unsigned long)l1_table | ARM_TABLE_DESCRIPTOR;
        
        // Fill L1 table with 1GB block mappings for identity mapping
        for (unsigned long l1_entry = 0; l1_entry < 512; l1_entry++) {
            unsigned long physical_addr = (block * 0x8000000000ULL) + (l1_entry * 0x40000000ULL);
            l1_table[l1_entry] = physical_addr | ARM_KERNEL_FLAGS;
        }
    }

    return page_table;
}


paddr_t virt_to_phys(page_table_t page_table, vaddr_t virtual_address)
{
    if (!page_table) return 0;
    
    page_table_indices_t idx = extract_indices(virtual_address);
    
    // Walk P0 (Level 0)
    unsigned long* p0 = (unsigned long*)page_table;
    if (!(p0[idx.p0_index] & ARM_TABLE_DESCRIPTOR)) {
        return 0; // No mapping at P0 level
    }
    unsigned long* p1 = (unsigned long*)(p0[idx.p0_index] & PAGE_MASK);
    
    // Check if this is a 1GB block (order 2)
    if (p1[idx.p1_index] & ARM_BLOCK_DESCRIPTOR) {
        // 1GB block - extract physical address
        paddr_t phys_base = p1[idx.p1_index] & ~0x3FFFFFFF; // Clear flags and mask to 1GB
        return phys_base + (virtual_address & 0x3FFFFFFF); // Add offset within 1GB block
    }
    
    // Walk P1 (Level 1) - must be a table descriptor for smaller pages
    if (!(p1[idx.p1_index] & ARM_TABLE_DESCRIPTOR)) {
        return 0; // No mapping at P1 level
    }
    unsigned long* p2 = (unsigned long*)(p1[idx.p1_index] & PAGE_MASK);
    
    // Check if this is a 2MB block (order 1)
    if (p2[idx.p2_index] & ARM_BLOCK_DESCRIPTOR) {
        // 2MB block - extract physical address
        paddr_t phys_base = p2[idx.p2_index] & ~0x1FFFFF; // Clear flags and mask to 2MB
        return phys_base + (virtual_address & 0x1FFFFF); // Add offset within 2MB block
    }
    
    // Walk P2 (Level 2) - must be a table descriptor for 4KB pages
    if (!(p2[idx.p2_index] & ARM_TABLE_DESCRIPTOR)) {
        return 0; // No mapping at P2 level
    }
    unsigned long* p3 = (unsigned long*)(p2[idx.p2_index] & PAGE_MASK);
    
    // Check if this is a 4KB page (order 0)
    if (p3[idx.p3_index] & ARM_PAGE_DESCRIPTOR) {
        // 4KB page - extract physical address
        paddr_t phys_base = p3[idx.p3_index] & PAGE_MASK; // Clear flags
        return phys_base + idx.offset; // Add page offset
    }
    
    return 0; // No valid mapping found
}

