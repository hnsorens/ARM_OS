#include "page_table.h"
#include "efidef.h"
#include "efierr.h"

#include "serial.h"

VOID*
Memset(
        IN VOID* Ptr, 
        IN UINT32 Value, 
        IN UINT64 N)
{
    UINT8* P = Ptr;
    for (UINT64 I = 0; I < N; ++I)
    {
        P[I] = (UINT64)Value;
    }
    return Ptr;
}

#define P0_INDEX(x) (((x) >> 39) & 0x1FF)
#define P1_INDEX(x) (((x) >> 30) & 0x1FF)
#define P2_INDEX(x) (((x) >> 21) & 0x1FF)
#define P3_INDEX(x) (((x) >> 12) & 0x1FF)

static PAGE_TABLE_INDICES Extract_Indices(IN UINT64 VirtualAddress) {
  PAGE_TABLE_INDICES Indices;
  Indices.Offset = VirtualAddress & 0xFFF;     /* Page offset (bits 0-11) */
  Indices.P3 = P3_INDEX(VirtualAddress); /* Page Table index */
  Indices.P2 = P2_INDEX(VirtualAddress); /* Page Directory index */
  Indices.P1 = P1_INDEX(VirtualAddress); /* PDPT index */
  Indices.P0 = P0_INDEX(VirtualAddress); /* PML4 index */
  return Indices;
}

static UINT64 Page_Order_Size(UINT64 Order)
{
  switch (Order) {
    case 0:
      return 4096;
    case 1:
      return 4096 * 512;
    case 2:
      return 4096 * 512 * 512;
  }
  return 0;
}

EFI_STATUS
Enable_Page_Table(
        IN PAGE_TABLE_T LowerPageTable, 
        IN PAGE_TABLE_T UpperPageTable
        )
{
    // Configure Memory Attributes
    UINT64 Mair = MAIR_ATTR(MAIR_NORMAL_WB, MAIR_IDX_NORMAL) |
                    MAIR_ATTR(MAIR_DEVICE_nGnRE, MAIR_IDX_DEVICE);
    __asm__ volatile("msr mair_el1, %0" : : "r"(Mair));

    Boot_Log("Set mair!\n", 10);

    // Configure Translation Control
    UINT64 Tcr = (TCR_TBI_DISABLE << TCR_TBI_SHIFT) |
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
    __asm__ volatile("msr tcr_el1, %0" : : "r"(Tcr));
    Boot_Log("Set TCR\n", 8);

    // Set Page Table Bases
    __asm__ volatile("msr ttbr0_el1, %0" : : "r"((UINT64)LowerPageTable));
    __asm__ volatile("msr ttbr1_el1, %0" : : "r"((UINT64)UpperPageTable));

    Boot_Log("Set page table pointers\n", 24);

    // Invalidate TLB
    __asm__ volatile("dsb sy");
    __asm__ volatile("tlbi vmalle1");
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");

    Boot_Log("Invalidated TLB\n", 16);

    // Enable MMU
    UINT64 Sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(Sctlr));
    Sctlr |= SCTLR_M_ENABLE | SCTLR_C_ENABLE | SCTLR_I_ENABLE;
    __asm__ volatile("msr sctlr_el1, %0" : : "r"(Sctlr));
    __asm__ volatile("isb");

    Boot_Log("Enabled MMU\n", 12);

    return EFI_SUCCESS;
}

EFI_STATUS
Map_Memory(
        IN EFI_SYSTEM_TABLE *SystemTable, 
        IN PAGE_TABLE_T* PageTable,
        IN EFI_VIRTUAL_ADDRESS VirtualAddress,
        IN EFI_PHYSICAL_ADDRESS PhysicalAddress,
        IN UINTN PageOrder, 
        IN UINTN PageCount
)
{   
    EFI_STATUS Status;

    if (!PageTable) {
        return EFI_INVALID_PARAMETER;
    }

    // Initialize root page table if it doesn't exist
    if (!*PageTable) {
        Status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiRuntimeServicesCode, 1, PageTable);
        if (EFI_ERROR(Status))
        {
            SystemTable->BootServices->FreePages(*PageTable, 1);
            return EFI_OUT_OF_RESOURCES;
        }

        Memset((void*)(*PageTable), 0, 4096);
    }
    
    EFI_PHYSICAL_ADDRESS *P0 = (EFI_PHYSICAL_ADDRESS*)(*PageTable);

    for (UINT64 I = 0; I < PageCount; I++) {
        EFI_VIRTUAL_ADDRESS CurrentVirtualAddress = VirtualAddress + I * Page_Order_Size(PageOrder);
        EFI_PHYSICAL_ADDRESS CurrentPhysicalAddress = PhysicalAddress + I * Page_Order_Size(PageOrder);
        PAGE_TABLE_INDICES Idx = Extract_Indices(CurrentVirtualAddress);

        // Get or create P1 table
        EFI_PHYSICAL_ADDRESS *P1;
        if (!(P0[Idx.P0] & ARM_TABLE_DESCRIPTOR)) {
            Status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiRuntimeServicesCode, 1, (EFI_PHYSICAL_ADDRESS*)(&P1));
            if (EFI_ERROR(Status))
            {
                SystemTable->BootServices->FreePages((EFI_PHYSICAL_ADDRESS)P1, 1);
                return EFI_OUT_OF_RESOURCES;
            }
            Memset(P1, 0, 4096);
            P0[Idx.P0] = (UINT64)P1 | ARM_TABLE_DESCRIPTOR;
        } else {
            P1 = (UINT64*)(P0[Idx.P0] & PAGE_MASK);
        }

        // 1GB pages (order 2)
        if (PageOrder == 2) {
            P1[Idx.P1] = CurrentPhysicalAddress | ARM_KERNEL_FLAGS;
            continue;
        }

        // Get or create P2 table
        UINT64 *P2;
        if (!(P1[Idx.P1] & ARM_TABLE_DESCRIPTOR)) {
            Status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiRuntimeServicesCode, 1, (EFI_PHYSICAL_ADDRESS*)(&P2));
            if (EFI_ERROR(Status))
            {
                SystemTable->BootServices->FreePages((EFI_PHYSICAL_ADDRESS)P2, 1);
                return EFI_OUT_OF_RESOURCES;
            }
            Memset(P2, 0, 4096);
            P1[Idx.P1] = (UINT64)P2 | ARM_TABLE_DESCRIPTOR;
        } else {
            P2 = (UINT64*)(P1[Idx.P1] & PAGE_MASK);
        }

        // 2MB pages (order 1)
        if (PageOrder == 1) {
            P2[Idx.P2] = CurrentPhysicalAddress | ARM_KERNEL_FLAGS;
            continue;
        }

        // Get or create P3 table for 4KB pages (order 0)
        EFI_PHYSICAL_ADDRESS *P3;
        if (!(P2[Idx.P2] & ARM_TABLE_DESCRIPTOR)) {
            Status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiRuntimeServicesCode, 1, (EFI_PHYSICAL_ADDRESS*)(&P3));
            if (EFI_ERROR(Status))
            {
                SystemTable->BootServices->FreePages((EFI_PHYSICAL_ADDRESS)P3, 1);
                return EFI_OUT_OF_RESOURCES;
            }
            Memset(P3, 0, 4096);
            P2[Idx.P2] = (UINT64)P3 | ARM_TABLE_DESCRIPTOR;
        } else {
            P3 = (UINT64*)(P2[Idx.P2] & PAGE_MASK);
        }

        // 4KB pages (order 0)
        if (PageOrder == 0) {
            P3[Idx.P3] = CurrentPhysicalAddress | ARM_4KB_PAGE_FLAGS;
            continue;
        }
    }

    return EFI_SUCCESS;
}

EFI_STATUS 
Create_Identity_Page_Table(
        IN EFI_SYSTEM_TABLE *SystemTable, 
        IN UINTN TotalMemory,
        OUT PAGE_TABLE_T *PageTable
)
{
    EFI_STATUS Status;
  // Allocate L0 table (512GB blocks)
Status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiRuntimeServicesCode, 1, PageTable);
            if (EFI_ERROR(Status))
            {
                SystemTable->BootServices->FreePages(*PageTable, 1);
                return Status;
            }
    Memset((VOID*)(*PageTable), 0, 4096);

    // Calculate how many 512GB blocks we need
    UINTN BlocksNeeded = (TotalMemory + 0x7FFFFFFFFFULL) / 0x8000000000ULL;
    if (BlocksNeeded == 0) BlocksNeeded = 1; // At least one block

    // For identity mapping, create 1GB block mappings in L1 tables
    for (UINT64 Block = 0; Block < BlocksNeeded; Block++) {
        // Allocate L1 table for this 512GB block
        EFI_PHYSICAL_ADDRESS L1 = 0;
        Status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiRuntimeServicesCode, 1, &L1);
        if (EFI_ERROR(Status))
        {
            SystemTable->BootServices->FreePages(L1, 1);
            return Status;
        }
        Memset((VOID*)L1, 0, 4096);
        
        // Set L0 entry to point to L1 table
        ((EFI_PHYSICAL_ADDRESS*)*PageTable)[Block] = (UINT64)L1 | ARM_TABLE_DESCRIPTOR;
        
        // Fill L1 table with 1GB block mappings for identity mapping
        for (UINT16 L1Entry = 0; L1Entry < 512; ++L1Entry) {
            UINT64 PhysicalAddress = (Block * 0x8000000000ULL) + (L1Entry * 0x40000000ULL);
            ((EFI_PHYSICAL_ADDRESS*)L1)[L1Entry] = PhysicalAddress | ARM_KERNEL_FLAGS;
        }
    }

    return EFI_SUCCESS;
}

