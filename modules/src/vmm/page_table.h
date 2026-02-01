#ifndef PAGE_TABLE_H
#define PAGE_TABLE_H

#include <stddef.h>
#include <stdint.h>

#define P0_INDEX(x) (((x) >> 39) & 0x1FF)
#define P1_INDEX(x) (((x) >> 30) & 0x1FF)
#define P2_INDEX(x) (((x) >> 21) & 0x1FF)
#define P3_INDEX(x) (((x) >> 12) & 0x1FF)

typedef uintptr_t phys_addr_t;
typedef uintptr_t virt_addr_t;
typedef unsigned long* page_table_t;

typedef struct page_table_indices_t
{
    uint16_t p0_index;
    uint16_t p1_index;
    uint16_t p2_index;
    uint16_t p3_index;
    uint16_t offset;
} page_table_indices_t;

#define ARM_TABLE_DESCRIPTOR         0x3
#define ARM_BLOCK_DESCRIPTOR         0x1
#define ARM_PAGE_DESCRIPTOR          0x3

#define ARM_MEMORY_DEVICE_nGnRnE     0x0
#define ARM_MEMORY_DEVICE_nGnRE      0x1
#define ARM_MEMORY_NORMAL_NC         0x2
#define ARM_MEMORY_NORMAL_WT         0x3
#define ARM_MEMORY_NORMAL_WB         0x4

#define ARM_AP_RW_EL1                (0x0 << 6)
#define ARM_AP_RO_EL1                (0x1 << 6)
#define ARM_AP_RW_EL0                (0x2 << 6)
#define ARM_AP_RO_EL0                (0x3 << 6)

#define ARM_ACCESS_FLAG              (1 << 10)
#define ARM_SH_NON_SHAREABLE         (0x0 << 8)
#define ARM_SH_OUTER_SHAREABLE       (0x2 << 8)
#define ARM_SH_INNER_SHAREABLE       (0x3 << 8)
#define ARM_ATTR_IDX(attr)           ((attr) << 2)

#define PAGE_MASK (~0xFFFULL)

// For kernel identity mapping
#define ARM_KERNEL_FLAGS             (ARM_BLOCK_DESCRIPTOR | \
                                     ARM_AP_RW_EL1 | \
                                     ARM_ACCESS_FLAG | \
                                     ARM_ATTR_IDX(ARM_MEMORY_NORMAL_WB) | \
                                     ARM_SH_INNER_SHAREABLE)

#define ARM_4KB_PAGE_FLAGS           (ARM_PAGE_DESCRIPTOR | \
                                     ARM_AP_RW_EL1 | \
                                     ARM_ACCESS_FLAG | \
                                     ARM_ATTR_IDX(ARM_MEMORY_NORMAL_WB) | \
                                     ARM_SH_INNER_SHAREABLE)


// TCR_EL1 Macros
#define TCR_TBI_SHIFT      37
#define TCR_IPS_SHIFT      32
#define TCR_TG1_SHIFT      30
#define TCR_SH1_SHIFT      28
#define TCR_ORGN1_SHIFT    26
#define TCR_IRGN1_SHIFT    24
#define TCR_T1SZ_SHIFT     16
#define TCR_TG0_SHIFT      14
#define TCR_SH0_SHIFT      12
#define TCR_ORGN0_SHIFT    10
#define TCR_IRGN0_SHIFT    8
#define TCR_T0SZ_SHIFT     0

#define TCR_TBI_DISABLE    0b00
#define TCR_IPS_40BIT      25UL
#define TCR_TG_4KB         0b00
#define TCR_SH_INNER       0b11
#define TCR_RGN_WB         0b01
#define TCR_T0SZ_48BIT     (64 - 48)

// MAIR_EL1 Macros  
#define MAIR_NORMAL_WB     0xFFUL
#define MAIR_DEVICE_nGnRE  0x44UL
#define MAIR_IDX_NORMAL    0
#define MAIR_IDX_DEVICE    1
#define MAIR_ATTR(attr, idx)   ((attr) << ((idx) * 8))

// SCTLR_EL1 Macros
#define SCTLR_M_ENABLE     (1 << 0)
#define SCTLR_C_ENABLE     (1 << 2)
#define SCTLR_I_ENABLE     (1 << 12)

/**
 * @brief Enables Paging for the OS
 *
 * @param page_table Initial page table
 */
void page_enable(page_table_t page_table);

/**
 * @brief Calculate memory needed for a bitmap
 * 
 * Computes total bytes required for bitmap structure and bit storage.
 * 
 * @param num_bits Number of bits the bitmap needs to store
 * @return Bytes needed for bitmap structure and storage
 */
void pages_map(page_table_t* page_table, virt_addr_t virtual_address, 
               phys_addr_t phys_addr, unsigned long page_order, unsigned long page_count);

/**
 * @brief Creates an identity page table for a specified amount of memory
 * 
 * @param ppm A physical memory manager for allocating page table
 * @param total_memory Total amount of system memory
 * @return New page table
 */
page_table_t pages_create_identity_page_table(size_t total_memory);

/**
 * @brief Converts virtual address to physical address
 * 
 * Looks virtual addresses up in a provided page table to find
 * it's corresponding physical address
 * 
 * @param page_table Page table to look up physical address
 * @param virtual_address Virtual address to look at on the page table
 * @return Physical address that corresponds with virtual address, will be 0 if there is no physical address mapped
 */
phys_addr_t virt_to_phys(page_table_t page_table, virt_addr_t virtual_address);

#endif