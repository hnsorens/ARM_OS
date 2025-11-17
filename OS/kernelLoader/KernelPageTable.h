#ifndef KERNEL_PAGE_TABLE_H
#define KERNEL_PAGE_TABLE_H

// ARM64 MMU definitions (define them yourself)
#include "Uefi.h"

#define ARM_PTE_BLOCK            (1UL << 0)
#define ARM_PTE_AF               (1UL << 10)
#define ARM_PTE_SH_INNER_SHAREABLE (3UL << 8)
#define ARM_PTE_AP_RW            (0UL << 6)
#define ARM_PTE_UXN              (1UL << 54)
#define ARM_PTE_PXN              (1UL << 53)
#define ARM_MEMORY_ATTRIBUTE_WRITE_BACK 4

#define MEMORY_512GB             (512ULL * 1024 * 1024 * 1024)
#define PAGE_SIZE                4096

EFI_STATUS
CreateIdentityPageTable1GB(
  IN EFI_SYSTEM_TABLE *SystemTable,
  OUT UINT64 **PageTablePtr
);

// Function to set up MMU registers using inline assembly
VOID
ConfigureArm64Mmu(
  IN UINT64 *PageTable
);

#endif