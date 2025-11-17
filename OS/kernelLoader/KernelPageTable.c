#include "KernelPageTable.h"

EFI_STATUS
CreateIdentityPageTable1GB(
  IN EFI_SYSTEM_TABLE *SystemTable,
  OUT UINT64 **PageTablePtr
)
{
  EFI_STATUS Status;
  UINT64 *L0Table;
  UINTN NumEntries;
  UINTN i;
  
  // FIX: Use EfiLoaderData instead of EfiBootServicesData
  Status = SystemTable->BootServices->AllocatePool(
    EfiLoaderData,  // ← CHANGED THIS LINE
    PAGE_SIZE,
    (VOID **)&L0Table
  );
  
  if (EFI_ERROR(Status) || L0Table == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  
  // Rest of your function stays the same...
  for (i = 0; i < (PAGE_SIZE / sizeof(UINT64)); i++) {
    L0Table[i] = 0;
  }
  
  NumEntries = MEMORY_512GB / SIZE_1GB;
  if (NumEntries > 512) {
    NumEntries = 512;
  }
  
  for (i = 0; i < NumEntries; i++) {
    UINT64 PhysicalBase = i * SIZE_1GB;
    UINT64 PteValue;
    
    PteValue = PhysicalBase |
               ARM_PTE_BLOCK |
               ARM_PTE_AF |
               ARM_PTE_SH_INNER_SHAREABLE |
               (ARM_MEMORY_ATTRIBUTE_WRITE_BACK << 2) |
               ARM_PTE_AP_RW |
               ARM_PTE_UXN | ARM_PTE_PXN;
    
    L0Table[i] = PteValue;
  }
  
  *PageTablePtr = L0Table;
  return EFI_SUCCESS;
}
// Function to set up MMU registers using inline assembly
VOID
ConfigureArm64Mmu(
  IN UINT64 *PageTable
)
{

  
  UINT64 Reg;
  
  // Write page table base to TTBR0_EL1
  __asm__ volatile("MSR TTBR0_EL1, %0" : : "r" ((UINT64)PageTable));

  while(1);
  
  // Configure TCR_EL1
  __asm__ volatile("MRS %0, TCR_EL1" : "=r" (Reg));
  Reg &= ~(0xFFFFUL << 16);  // Clear existing configuration
  Reg |= (0x0UL << 14) |     // TG0 = 4KB
         (0x2UL << 12) |     // SH0 = Inner Shareable
         (0x1UL << 10) |     // ORGN0 = Normal Write-Back
         (0x1UL << 8) |      // IRGN0 = Normal Write-Back
         (16UL << 0);        // T0SZ = 16 (48-bit VA space)
  __asm__ volatile("MSR TCR_EL1, %0" : : "r" (Reg));
  
  // Invalidate TLB
  __asm__ volatile("TLBI VMALLE1");
  __asm__ volatile("DSB SY");
  __asm__ volatile("ISB");
  
  // Enable MMU in SCTLR_EL1
  __asm__ volatile("MRS %0, SCTLR_EL1" : "=r" (Reg));
  Reg |= (1UL << 0) |  // M bit = Enable MMU
         (1UL << 2) |  // C bit = Enable data cache
         (1UL << 12);  // I bit = Enable instruction cache
  __asm__ volatile("MSR SCTLR_EL1, %0" : : "r" (Reg));
  __asm__ volatile("ISB");
}