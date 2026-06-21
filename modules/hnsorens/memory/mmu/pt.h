/**
 * @file pt.h
 * @brief Public Architectural Definitions and Translation Vector Interfaces for the AArch64 MMU Engine.
 */

#ifndef PAGE_TABLE_H
#define PAGE_TABLE_H

#include <stddef.h>
#include <stdint.h>
#include <api/mmu.h>

/* --- Virtual Address Index Extraction Macros --- */
#define P0_INDEX(x) (((x) >> 39) & 0x1FF)
#define P1_INDEX(x) (((x) >> 30) & 0x1FF)
#define P2_INDEX(x) (((x) >> 21) & 0x1FF)
#define P3_INDEX(x) (((x) >> 12) & 0x1FF)

/**
 * @struct pt_indices
 * @brief Structural layout tracking index metrics extracted from raw virtual address targets.
 */
struct pt_indices
{
    u16 l0_index;  /**< Level 0 Page Directory Translation Map Pointer Index */
    u16 l1_index;  /**< Level 1 Page Directory Translation Map Pointer Index (1GB Blocks) */
    u16 l2_index;  /**< Level 2 Page Directory Translation Map Pointer Index (2MB Blocks) */
    u16 l3_index;  /**< Level 3 Page Directory Translation Map Pointer Index (4KB Pages) */
    u16 offset;    /**< Terminal byte offset criteria into standard raw page granules */
};

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
 * @param[out] out_root Destination storage location for the root frame physical address.
 * @return int Operational execution status context values.
 */
int pt_alloc(u64 *out_root);

/**
 * @brief Destroys and cleans up nested page tables recursively.
 * @param[in] root Physical address of the root L0 page directory tree.
 * @return int Operational execution status context values.
 */
int pt_free(u64 root);

/**
 * @brief Generates an independent deep copy clone of an active mapping tree context.
 * @param[in]  src_root  Physical address of the template source tree.
 * @param[out] dest_root Destination address to output the cloned tree base frame address.
 * @return int Operational execution status context values.
 */
int pt_copy(u64 src_root, u64 *dest_root);

/**
 * @brief Binds a user mapping directory tree and its ASID into hardware TTBR0_EL1.
 * @param[in] root Physical base address hosting the targeted user space directory.
 * @param[in] asid Hardware Address Space Identifier context tag tracking code.
 * @return int Operational execution status context values.
 */
int pt_set_user_ctx(u64 root, u16 asid);

/**
 * @brief Binds a secure kernel directory tree and its ASID into hardware TTBR1_EL1.
 * @param[in] root Physical base address anchoring the kernel space page structure.
 * @param[in] asid Hardware Address Space Identifier context tag tracking code.
 * @return int Operational execution status context values.
 */
int pt_set_kernel_ctx(u64 root, u16 asid);

/**
 * @brief Generates coherent structural mappings linking virtual ranges to physical target sectors.
 * @param[in] root     Physical root anchor point targeting the Level 0 system translation structure table.
 * @param[in] virt     Target base virtual range start pointer location frame coordinates.
 * @param[in] phys     Source destination frame tracking alignment memory physical mapping start points.
 * @param[in] pg_count Total contiguous page allocation chunks to process.
 * @param[in] pg_size  Sizing layout attribute configurations (PS_4KB, PS_2MB, PS_1GB).
 * @param[in] f        Architectural allocation flags (Read/Write access, execution locks, cache controls).
 * @return int Operational execution status context values.
 */
int pt_map(u64 root, u64 virt, u64 phys, u64 pg_count, enum page_size pg_size, enum mmu_flags f);

/**
 * @brief Tears down page map links across virtual spaces and dynamically prunes unneeded tables.
 * @param[in] root     Physical address hosting the parent Level 0 structure tree base space.
 * @param[in] virt     Targeted virtual memory tracking path path start coordinate to unmap.
 * @param[in] pg_count Contiguous volume tracker indicating the exact range width to process.
 * @param[in] pg_size  Page size tracking definition attributes (PS_4KB, PS_2MB, PS_1GB).
 * @return int Operational execution status context values.
 */
int pt_unmap(u64 root, u64 virt, u64 pg_count, enum page_size pg_size);

/**
 * @brief Adjusts attribute protection settings across target virtual translation blocks.
 * @param[in] root     Physical location anchor hosting the translation structure base directory frame.
 * @param[in] virt     Target memory block destination virtual map path start point context.
 * @param[in] pg_count Contiguous volume tracker stating the range footprint limit.
 * @param[in] pg_size  Sizing attributes determining table alignment (PS_4KB, PS_2MB, PS_1GB).
 * @param[in] f        Execution restrictions and flag modifiers to safely apply to the targets.
 * @return int Operational execution status context values.
 */
int pt_protect(u64 root, u64 virt, u64 pg_count, enum page_size pg_size, enum mmu_flags f);

/**
 * @brief Manually resolves custom virtual path lines down to physical hardware addresses.
 * @param[in]  root      Physical entry base destination anchoring the structural Level 0 table frame space.
 * @param[in]  virt      Target memory tracking path virtual address location to query.
 * @param[out] phys_out  Output variable used to record recovered destination physical address coordinates.
 * @param[out] flags_out Output variable used to store extracted page translation attributes.
 * @return int Operational execution status context values.
 */
int pt_translate(u64 root, u64 virt, u64 *phys_out, enum mmu_flags *flags_out);

/**
 * @brief Flushes the entire TLB cache hardware tracking table across all active SMP cores.
 * @return int Operational execution status context values.
 */
int pt_flush(void);

/**
 * @brief Evicts range explicit address context structures out of the TLB pipeline.
 * @param[in] virt     Target memory block destination virtual translation base pointer location to invalidate.
 * @param[in] pg_count Contiguous step boundaries tracker stating overall range footprint limits.
 * @param[in] pg_size  Sizing configurations matching original translation block properties.
 * @return int Operational execution status context values.
 */
int pt_invalidate(u64 virt, u64 pg_count, enum page_size pg_size);

/**
 * @brief Configures MAIR profile attributes inside the host register file vector.
 * @param[in] mair_value The absolute 64-bit multi-attribute profile tracking bitmask.
 * @return int Operational execution status context values.
 */
int pt_set_mair(u64 mair_value);

/**
 * @brief Extracts the physical base register values committed into hardware register map systems.
 * @param[out] root Destination target allocation parameter to track.
 * @return int Operational execution status context values.
 */
int pt_get_user_ctx(u64 *root);

/**
 * @brief Extracts the physical base register values committed into secure kernel hardware vectors.
 * @param[out] root Destination target allocation parameter to track.
 * @return int Operational execution status context values.
 */
int pt_get_kernel_ctx(u64 *root);

#endif
