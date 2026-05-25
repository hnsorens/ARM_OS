#ifndef PAGE_TABLE_H
#define PAGE_TABLE_H

#include <efi.h>
#include <efilib.h>

#define P0_INDEX(x) (((x) >> 39) & 0x1FF)
#define P1_INDEX(x) (((x) >> 30) & 0x1FF)
#define P2_INDEX(x) (((x) >> 21) & 0x1FF)
#define P3_INDEX(x) (((x) >> 12) & 0x1FF)

typedef EFI_PHYSICAL_ADDRESS PAGE_TABLE_T;

typedef struct PAGE_TABLE_INDICES
{
    UINT16 P0;
    UINT16 P1;
    UINT16 P2;
    UINT16 P3;
    UINT16 Offset;
} PAGE_TABLE_INDICES;

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

#define TCR_TBI_DISABLE    0b00ULL
#define TCR_IPS_40BIT      0b10ULL
#define TCR_TG_4KB         0b00ULL
#define TCR_SH_INNER       0b11ULL
#define TCR_RGN_WB         0b01ULL
#define TCR_T0SZ_48BIT     (64ULL - 48ULL)

// MAIR_EL1 Macros  
#define MAIR_NORMAL_WB     0xFFUL
#define MAIR_DEVICE_nGnRE  0x04UL
#define MAIR_IDX_NORMAL    0
#define MAIR_IDX_DEVICE    1
#define MAIR_ATTR(attr, idx)   ((attr) << ((idx) * 8))

// SCTLR_EL1 Macros
#define SCTLR_M_ENABLE     (1 << 0)
#define SCTLR_C_ENABLE     (1 << 2)
#define SCTLR_I_ENABLE     (1 << 12)


EFI_STATUS
Enable_Page_Table(
        IN PAGE_TABLE_T LowerPageTable, 
        IN PAGE_TABLE_T UpperPageTable
);


EFI_STATUS
Map_Memory(
        IN EFI_SYSTEM_TABLE *SystemTable, 
        IN PAGE_TABLE_T* PageTable,
        IN EFI_VIRTUAL_ADDRESS VirtualAddress,
        IN EFI_PHYSICAL_ADDRESS PhysicalAddress,
        IN UINTN PageOrder, 
        IN UINTN PageCount
);

EFI_STATUS 
Create_Identity_Page_Table(
        IN EFI_VIRTUAL_ADDRESS Start,
        IN EFI_SYSTEM_TABLE *SystemTable, 
        IN UINTN TotalMemory,
        OUT PAGE_TABLE_T *PageTable
);

#endif
