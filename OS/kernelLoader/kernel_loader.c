#include "kernel_loader.h"
#include "Base.h"
#include "KernelPageTable.h"
#include "ProcessorBind.h"
#include "Uefi/UefiBaseType.h"
#include "Uefi/UefiMultiPhase.h"
#include "Uefi/UefiSpec.h"
#include "memory/buddy_allocator.h"
#include "memory/page_table.h"
#include "uefi/helpers.h"
#include <stdint.h>

#define PRINT(str) SystemTable->ConOut->OutputString(SystemTable->ConOut, str);

typedef struct
{
  UINTN MemoryMapSize;
  EFI_MEMORY_DESCRIPTOR* MemoryMap;
  UINTN MapKey;
  UINTN DescriptorSize;
  UINT32 DescriptorVersion;
} EFI_MEMORY_MAP;

[[gnu::unused]]
static EFI_STATUS
ExitBootServices(
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE   *SystemTable,
  IN EFI_MEMORY_MAP     *MemoryMap
)
{
  MemoryMap->MemoryMapSize = 0;
  MemoryMap->MemoryMap = NULL;
  MemoryMap->MapKey = 0;
  MemoryMap->DescriptorSize = 0;
  MemoryMap->DescriptorVersion = 0;

  // get required memory map buffer size
  EFI_STATUS Status = SystemTable->BootServices->GetMemoryMap(
    &MemoryMap->MemoryMapSize,
    MemoryMap->MemoryMap,
    &MemoryMap->MapKey,
    &MemoryMap->DescriptorSize,
    &MemoryMap->DescriptorVersion
  );

  if (Status != EFI_BUFFER_TOO_SMALL)
  {
    PRINT(u"Unexpected error getting memory map size!");
    return Status;
  }

  // allocate space for memory map buffer
  MemoryMap->MemoryMapSize += 2 * MemoryMap->DescriptorSize;
  Status = SystemTable->BootServices->AllocatePool(EfiLoaderData, MemoryMap->MemoryMapSize, (void**)&MemoryMap->MemoryMap);

  if (EFI_ERROR(Status))
  {
    PRINT(u"Failed to allocate memory map buffer!");
    return Status;
  }

  // populate memory map buffer with memory map
  Status = SystemTable->BootServices->GetMemoryMap(
    &MemoryMap->MemoryMapSize,
    MemoryMap->MemoryMap,
    &MemoryMap->MapKey,
    &MemoryMap->DescriptorSize,
    &MemoryMap->DescriptorVersion
  );

  if (EFI_ERROR(Status))
  {
    PRINT(u"Failed to get memory map!");
    return Status;
  }

  for (int i = 0; i < MemoryMap->MemoryMapSize / MemoryMap->DescriptorSize; i++)
  {
    EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*)((size_t)MemoryMap->MemoryMap + MemoryMap->DescriptorSize * i);
    PRINT(u"SIZE: ");
    PrintNumber(SystemTable, desc->NumberOfPages);
    PRINT(u" PHYS ");
    PrintNumber(SystemTable, desc->PhysicalStart);
    PRINT(u" VIRT ");
    PrintNumber(SystemTable, desc->VirtualStart);
    PRINT(u" IDK ");
    PrintNumber(SystemTable, desc->Type);

    switch (desc->Type)
    {
      case EfiConventionalMemory:
        PRINT(u"EfiConventionalMemory");
        break;
      case EfiReservedMemoryType:
        PRINT(u"EfiReservedMemoryType");
        break;
      case EfiMaxMemoryType:
        PRINT(u"EfiMaxMemoryType");
        break;
      case EfiUnacceptedMemoryType:
        PRINT(u"EfiUnacceptedMemoryType");
        break;
    }
    
    PRINT(u"\n");
  }

  // exit boot services
  Status = SystemTable->BootServices->ExitBootServices(ImageHandle, MemoryMap->MapKey);
  if (Status == EFI_INVALID_PARAMETER)
  {
    // Memory map changed, try again
    Status = SystemTable->BootServices->GetMemoryMap(
      &MemoryMap->MemoryMapSize,
      MemoryMap->MemoryMap,
      &MemoryMap->MapKey,
      &MemoryMap->DescriptorSize,
      &MemoryMap->DescriptorVersion
    );

    if (EFI_ERROR(Status))
    {
      PRINT(u"Failed to get updated memory map!");
      SystemTable->BootServices->FreePool(MemoryMap->MemoryMap);
      return Status;
    }

    Status = SystemTable->BootServices->ExitBootServices(ImageHandle, MemoryMap->MapKey);
  }

  if (EFI_ERROR(Status))
  {
    PRINT(u"ExitBootServices failed!");
    SystemTable->BootServices->FreePool(MemoryMap->MemoryMap);
    return Status;
  }

  return EFI_SUCCESS;
}

typedef struct
{
  UINTN Physical_Base;
  UINTN Virtual_Base;
  UINTN Size;
} KERNEL_REGION;

EFI_STATUS
AllocateKernelMemoryRegions(
  EFI_MEMORY_MAP  *MemoryMap,
  KERNEL_REGION   *Regions,
  UINTN           RegionCount
)
{
  UINTN EfiRegionCount = MemoryMap->MemoryMapSize / MemoryMap->DescriptorSize;
  EFI_MEMORY_DESCRIPTOR* CurrentEntry = MemoryMap->MemoryMap;
  UINTN ResolvedRegions = 0;
  for (UINTN i = 0; i < EfiRegionCount; i++)
  {
    CurrentEntry = MemoryMap->MemoryMap + i;
    if (CurrentEntry->Type == EfiConventionalMemory)
    {
      for (UINTN j = 0; j < RegionCount; j++)
      {
        CurrentEntry = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)CurrentEntry + MemoryMap->DescriptorSize);
        if (Regions[j].Physical_Base == 0 && Regions[j].Virtual_Base == 0 && CurrentEntry->NumberOfPages >= Regions[j].Size)
        {
          Regions[j].Physical_Base = CurrentEntry->PhysicalStart;
          Regions[j].Virtual_Base = CurrentEntry->VirtualStart;
          CurrentEntry->PhysicalStart += Regions[j].Size * 4096;
          CurrentEntry->VirtualStart += Regions[j].Size * 4096;
          CurrentEntry->NumberOfPages -= Regions[j].Size;
          ResolvedRegions++;
        }
      }
    }
  }

  return ResolvedRegions != RegionCount ? EFI_OUT_OF_RESOURCES : EFI_SUCCESS;
}

EFI_STATUS
GetTotalMemory(
  IN EFI_MEMORY_MAP   *MemoryMap,
  IN UINTN            *MemorySize
)
{
  if (!MemoryMap || !MemoryMap->MemoryMapSize)
  {
    return EFI_INVALID_PARAMETER;
  }

  UINTN EfiRegionCount = MemoryMap->MemoryMapSize / MemoryMap->DescriptorSize;
  UINTN MemorySum = 0;
  for (int i = 0; i < EfiRegionCount; i++)
  {
    EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*)((size_t)MemoryMap->MemoryMap + MemoryMap->DescriptorSize * i);
    MemorySum += desc->NumberOfPages * 4096;
  }

  *MemorySize = MemorySum;
  return EFI_SUCCESS;
}

EFI_STATUS
LoadKernel(
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE   *SystemTable
)
{
  EFI_MEMORY_MAP MemoryMap;

  UINT64* PageTablePtr;
  CreateIdentityPageTable1GB(SystemTable, &PageTablePtr);

  PrintNumber(SystemTable, (UINT64)PageTablePtr);
  
  // ConfigureArm64Mmu(PageTablePtr);
  

  while(1);

  EFI_STATUS Status = ExitBootServices(
    ImageHandle,
    SystemTable,
    &MemoryMap
  );
  
  if (EFI_ERROR(Status))
  {
    return Status;
  }

  UINTN MemorySize;
  Status = GetTotalMemory(
    &MemoryMap,
    &MemorySize
  );

  if (EFI_ERROR(Status))
  {
    return Status;
  }

  UINTN buddy_bitmap_memory = buddy_get_memory_size(
    MemorySize
  );

  size_t page_table_pages = get_kernel_page_table_page_count(MemorySize);

  #define REGION(size) {0, 0, (size)}

  KERNEL_REGION Regions[] = {
    REGION((buddy_bitmap_memory / 4096) + 1),    // buddy allocator bitmap
    REGION(1),                                   // buddy allocator
    REGION(page_table_pages)                    // Page table 
  };

  Status = AllocateKernelMemoryRegions(
    &MemoryMap,
    Regions,
    sizeof(Regions) / sizeof(KERNEL_REGION)
  );

  create_kernel_page_table(Regions[2].Physical_Base);

  //buddy_init(
  //  MemoryMap.MemoryMap,
  //  MemoryMap.MemoryMapSize,
  //  (buddy_allocator_t*)Regions[1].Virtual_Base,
  //  Regions[0].Physical_Base,
  //  MemorySize
  //);

  return Status;
}
