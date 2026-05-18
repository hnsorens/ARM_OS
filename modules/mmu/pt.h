#ifndef PAGE_TABLE_H
#define PAGE_TABLE_H

#include <stddef.h>
#include <stdint.h>
#include "../../include/api/mmu.h"

/* --- Virtual Address Index Extraction Macros --- */
#define P0_INDEX(x) (((x) >> 39) & 0x1FF)
#define P1_INDEX(x) (((x) >> 30) & 0x1FF)
#define P2_INDEX(x) (((x) >> 21) & 0x1FF)
#define P3_INDEX(x) (((x) >> 12) & 0x1FF)

typedef uint64_t pte_t;

/* Deconstructed components of a 4-level virtual address translation path */
typedef struct pt_indices
{
    uint16_t l0_index;
    uint16_t l1_index;
    uint16_t l2_index;
    uint16_t l3_index;
    uint16_t offset;
} pt_indices_t;

/* --- AArch64 Descriptor Type Encodings --- */
#define ARM_TABLE_DESCRIPTOR         0x3
#define ARM_BLOCK_DESCRIPTOR         0x1
#define ARM_PAGE_DESCRIPTOR          0x3

/* --- MAIR Memory Attribute Profile Indicators --- */
#define ARM_MEMORY_DEVICE_nGnRnE     0x0
#define ARM_MEMORY_DEVICE_nGnRE      0x1
#define ARM_MEMORY_NORMAL_NC         0x2
#define ARM_MEMORY_NORMAL_WT         0x3
#define ARM_MEMORY_NORMAL_WB         0x4

/* --- Access Permissions (AP Field Encodings) --- */
#define ARM_AP_RW_EL1                (0x0 << 6)
#define ARM_AP_RO_EL1                (0x1 << 6)
#define ARM_AP_RW_EL0                (0x2 << 6)
#define ARM_AP_RO_EL0                (0x3 << 6)

/* --- Core Page Descriptor Configuration Flags --- */
#define ARM_ACCESS_FLAG              (1 << 10)
#define ARM_SH_NON_SHAREABLE         (0x0 << 8)
#define ARM_SH_OUTER_SHAREABLE       (0x2 << 8)
#define ARM_SH_INNER_SHAREABLE       (0x3 << 8)
#define ARM_ATTR_IDX(attr)           ((attr) << 2)

#define PAGE_MASK (~0xFFFULL)

/* --- Common Default Kernel Architectural Attributes --- */
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

/* --- TCR_EL1 Field Control Shift Constraints --- */
#define TCR_TBI_SHIFT       37
#define TCR_IPS_SHIFT       32
#define TCR_TG1_SHIFT       30
#define TCR_SH1_SHIFT       28
#define TCR_ORGN1_SHIFT     26
#define TCR_IRGN1_SHIFT     24
#define TCR_T1SZ_SHIFT      16
#define TCR_TG0_SHIFT       14
#define TCR_SH0_SHIFT       12
#define TCR_ORGN0_SHIFT     10
#define TCR_IRGN0_SHIFT     8
#define TCR_T0SZ_SHIFT      0

/* --- TCR_EL1 Operational Parameter Encodings --- */
#define TCR_TBI_DISABLE     0b00
#define TCR_IPS_40BIT       25
#define TCR_TG_4KB          0b00
#define TCR_SH_INNER        0b11
#define TCR_RGN_WB          0b01
#define TCR_T0SZ_48BIT      (64 - 48)

/* --- MAIR_EL1 Allocation Layout Utilities --- */
#define MAIR_NORMAL_WB       0xFF
#define MAIR_DEVICE_nGnRE    0x44
#define MAIR_IDX_NORMAL      0
#define MAIR_IDX_DEVICE      1
#define MAIR_ATTR(attr, idx) ((attr) << ((idx) * 8))

/* --- SCTLR_EL1 Controls --- */
#define SCTLR_M_ENABLE       (1 << 0)
#define SCTLR_C_ENABLE       (1 << 2)
#define SCTLR_I_ENABLE       (1 << 12)


/* --- Exported Page Table Subsystem Public API --- */

/**
 * @brief Allocates an empty 4KB physical page frame to act as a root L0 table.
 * @param out_root Destination storage location for the root frame physical address.
 * @return k_status_t Execution confirmation code.
 */
k_status_t pt_alloc(paddr_t *out_root);

/**
 * @brief Destroys and cleans up nested page tables recursively.
 * @param root Physical address of the root L0 page directory tree.
 * @return k_status_t Execution confirmation code.
 */
k_status_t pt_free(paddr_t root);

/**
 * @brief Generates an independent deep copy clone of an active mapping tree context.
 * @param src_root Physical address of the template source tree.
 * @param dest_root Destination address to output the cloned tree base frame address.
 * @return k_status_t Execution confirmation code.
 */
k_status_t pt_copy(paddr_t src_root, paddr_t *dest_root);

/**
 * @brief Binds a user mapping directory tree and its ASID into hardware TTBR0_EL1.
 * @param root Physical base address hosting the targeted user space directory.
 * @param asid Hardware Address Space Identifier context tag tracking code.
 * @return k_status_t Execution confirmation code.
 */
k_status_t pt_set_user_ctx(paddr_t root, asid_t asid);

/**
 * @brief Binds a secure kernel directory tree and its ASID into hardware TTBR1_EL1.
 * @param root Physical base address anchoring the kernel space page structure.
 * @param asid Hardware Address Space Identifier context tag tracking code.
 * @return k_status_t Execution confirmation code.
 */
k_status_t pt_set_kernel_ctx(paddr_t root, asid_t asid);

/**
 * @brief Generates coherent structural mappings linking virtual ranges to physical target sectors.
 * @param root Physical root anchor point targeting the Level 0 system translation structure table.
 * @param virt Target base virtual range start pointer location frame coordinates.
 * @param phys Source destination frame tracking alignment memory physical mapping start points.
 * @param pg_count Total contiguous page allocation chunks to process.
 * @param pg_size Sizing layout attribute configurations (PS_4KB, PS_2MB, PS_1GB).
 * @param f Architectural allocation flags (Read/Write access, execution locks, cache controls).
 * @return k_status_t Execution confirmation code.
 */
k_status_t pt_map(paddr_t root, vaddr_t virt, paddr_t phys, uint64_t pg_count, page_size_t pg_size, mmu_flags_t f);

/**
 * @brief Tears down page map links across virtual spaces and dynamically prunes unneeded tables.
 * @param root Physical address hosting the parent Level 0 structure tree base space.
 * @param virt Targeted virtual memory tracking path path start coordinate to unmap.
 * @param pg_count Contiguous volume tracker indicating the exact range width to process.
 * @param pg_size Page size tracking definition attributes (PS_4KB, PS_2MB, PS_1GB).
 * @return k_status_t Execution confirmation code.
 */
k_status_t pt_unmap(paddr_t root, vaddr_t virt, uint64_t pg_count, page_size_t pg_size);

/**
 * @brief Adjusts attribute protection settings across target virtual translation blocks.
 * @param root Physical location anchor hosting the translation structure base directory frame.
 * @param virt Target memory block destination virtual map path start point context.
 * @param pg_count Contiguous volume tracker stating the range footprint limit.
 * @param pg_size Sizing attributes determining table alignment (PS_4KB, PS_2MB, PS_1GB).
 * @param f Execution restrictions and flag modifiers to safely apply to the targets.
 * @return k_status_t Execution confirmation code.
 */
k_status_t pt_protect(paddr_t root, vaddr_t virt, uint64_t pg_count, page_size_t pg_size, mmu_flags_t f);

/**
 * @brief Manually resolves custom virtual path lines down to physical hardware addresses.
 * @param root Physical entry base destination anchoring the structural Level 0 table frame space.
 * @param virt Target memory tracking path virtual address location to query.
 * @param phys_out Output variable used to record recovered destination physical address coordinates.
 * @param flags_out Output variable used to store extracted page translation attributes.
 * @return k_status_t Execution confirmation code.
 */
k_status_t pt_translate(paddr_t root, vaddr_t virt, paddr_t *phys_out, mmu_flags_t *flags_out);

/**
 * @brief Flushes the entire TLB cache hardware tracking table across all active SMP cores.
 * @return k_status_t Execution confirmation code.
 */
k_status_t pt_flush(void);

/**
 * @brief Evicts range explicit address context structures out of the TLB pipeline.
 * @param virt Target memory block destination virtual translation base pointer location to invalidate.
 * @param pg_count Contiguous step boundaries tracker stating overall range footprint limits.
 * @param pg_size Sizing configurations matching original translation block properties.
 * @return k_status_t Execution confirmation code.
 */
k_status_t pt_invalidate(vaddr_t virt, uint64_t pg_count, page_size_t pg_size);

/**
 * @brief Configures MAIR profile attributes inside the host register file vector.
 * @param mair_value The absolute 64-bit multi-attribute profile tracking bitmask.
 * @return k_status_t Execution confirmation code.
 */
k_status_t pt_set_mair(uint64_t mair_value);

#endif
