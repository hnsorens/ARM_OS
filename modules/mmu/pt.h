#ifndef PAGE_TABLE_H
#define PAGE_TABLE_H

#include <stddef.h>
#include <stdint.h>
#include "../../include/api/mmu.h"

#define P0_INDEX(x) (((x) >> 39) & 0x1FF)
#define P1_INDEX(x) (((x) >> 30) & 0x1FF)
#define P2_INDEX(x) (((x) >> 21) & 0x1FF)
#define P3_INDEX(x) (((x) >> 12) & 0x1FF)

typedef uint64_t pte_t;

typedef struct pt_indices
{
    uint16_t l0_index;
    uint16_t l1_index;
    uint16_t l2_index;
    uint16_t l3_index;
    uint16_t offset;
} pt_indices_t;

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
#define TCR_IPS_40BIT      25
#define TCR_TG_4KB         0b00
#define TCR_SH_INNER       0b11
#define TCR_RGN_WB         0b01
#define TCR_T0SZ_48BIT     (64 - 48)

// MAIR_EL1 Macros  
#define MAIR_NORMAL_WB     0xFF
#define MAIR_DEVICE_nGnRE  0x44
#define MAIR_IDX_NORMAL    0
#define MAIR_IDX_DEVICE    1
#define MAIR_ATTR(attr, idx)   ((attr) << ((idx) * 8))

// SCTLR_EL1 Macros
#define SCTLR_M_ENABLE     (1 << 0)
#define SCTLR_C_ENABLE     (1 << 2)
#define SCTLR_I_ENABLE     (1 << 12)

k_status_t pt_alloc(paddr_t *out_root);
k_status_t pt_free(paddr_t root);
k_status_t pt_copy(paddr_t src_root, paddr_t *dest_root);

k_status_t pt_set_user_ctx(paddr_t root, asid_t asid);
k_status_t pt_set_kernel_ctx(paddr_t root, asid_t asid);

k_status_t pt_map(paddr_t root, vaddr_t virt, paddr_t phys, uint64_t pg_count, page_size_t pg_size, mmu_flags_t f);
k_status_t pt_unmap(paddr_t root, vaddr_t virt, uint64_t pg_count, page_size_t pg_size);
k_status_t pt_protect(paddr_t root, vaddr_t virt, uint64_t pg_count, page_size_t pg_size, mmu_flags_t f);

k_status_t pt_translate(paddr_t root, vaddr_t virt, paddr_t *phys_out, mmu_flags_t *flags_out);

k_status_t pt_flush(void);
k_status_t pt_invalidate(vaddr_t virt, uint64_t pg_count, page_size_t pg_size);
k_status_t pt_set_mair(uint64_t mair_value);

#endif
