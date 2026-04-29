#include "boot_services.h"



#define MEMORY_MAP_ENTRY_INDEX(Index) ((EFI_MEMORY_DESCRIPTOR*)((char*)EfiMemoryMap + RegionSize * Index))
static
VOID
SwapEfiMemoryRegions(
  IN OUT EFI_MEMORY_DESCRIPTOR   *EfiMemoryMap,
  IN UINTN                        RegionSize,
  IN UINTN                        i,
  IN UINTN                        j
)
{
  EFI_MEMORY_DESCRIPTOR TempDescriptor = *MEMORY_MAP_ENTRY_INDEX(i);
  EFI_MEMORY_DESCRIPTOR *DstDescriptor = MEMORY_MAP_ENTRY_INDEX(i);
  EFI_MEMORY_DESCRIPTOR *SrcDescriptor = MEMORY_MAP_ENTRY_INDEX(j);
  DstDescriptor->Attribute = SrcDescriptor->Attribute;
  DstDescriptor->NumberOfPages = SrcDescriptor->NumberOfPages;
  DstDescriptor->PhysicalStart = SrcDescriptor->PhysicalStart;
  DstDescriptor->VirtualStart = SrcDescriptor->VirtualStart;
  DstDescriptor->Type = SrcDescriptor->Type;

  SrcDescriptor->Attribute = TempDescriptor.Attribute;
  SrcDescriptor->NumberOfPages = TempDescriptor.NumberOfPages;
  SrcDescriptor->PhysicalStart = TempDescriptor.PhysicalStart;
  SrcDescriptor->VirtualStart = TempDescriptor.VirtualStart;
  SrcDescriptor->Type = TempDescriptor.Type;
}

static 
UINTN 
Partition(
  IN OUT EFI_MEMORY_DESCRIPTOR   *EfiMemoryMap,
  IN UINTN                        RegionSize,
  IN UINTN                        Low,
  IN UINTN                        High
)
{
  UINTN Pivot = MEMORY_MAP_ENTRY_INDEX(High)->PhysicalStart;
  UINTN i = (Low - 1);

  for (UINTN j = Low; j < High; j++)
  {
    if (MEMORY_MAP_ENTRY_INDEX(j)->PhysicalStart <= Pivot)
    {
      i++;
      SwapEfiMemoryRegions(EfiMemoryMap, RegionSize, i, j);
    }
  }
  SwapEfiMemoryRegions(EfiMemoryMap, RegionSize, i+1, High);
  return (i + 1);
}

static 
VOID 
SortEfiMemoryMap(
  IN OUT EFI_MEMORY_DESCRIPTOR   *EfiMemoryMap,
  IN UINTN                        RegionSize,
  IN UINTN                        Low,
  IN UINTN                        High
)
{
  if (Low < High)
  {
    UINTN PartitionIndex = Partition(EfiMemoryMap, RegionSize, Low, High);
    
    SortEfiMemoryMap(EfiMemoryMap, RegionSize, Low, PartitionIndex - 1);
    SortEfiMemoryMap(EfiMemoryMap, RegionSize, PartitionIndex + 1, High);
  }
}

#undef MEMORY_MAP_ENTRY_INDEX

static EFI_STATUS GetKernelMemoryMap(
  IN  EFI_MEMORY_MAP    *EfiMemoryMap,
  OUT MEMORY_MAP        *KernelMemoryMap,
  OUT UINTN             *MemoryMapRegionCount,
  EFI_SYSTEM_TABLE *SystemTable
)
{
  UINTN MemoryMapEntryCount = EfiMemoryMap->MemoryMapSize / EfiMemoryMap->DescriptorSize;


  SortEfiMemoryMap(
    EfiMemoryMap->MemoryMap,
    EfiMemoryMap->DescriptorSize,
    0,
    MemoryMapEntryCount - 1
  );

  PHYSICAL_ADDRESS MemoryMapAddr;
  
  EFI_MEMORY_DESCRIPTOR* Current = EfiMemoryMap->MemoryMap;


  UINTN MemoryMapPageCount = 1;
  for (int i = 0; i < MemoryMapEntryCount; i++)
  {
    if (Current->Type == EfiConventionalMemory && Current->NumberOfPages >= MemoryMapPageCount)
    {
      Current->NumberOfPages -= MemoryMapPageCount;
      MemoryMapAddr = Current->PhysicalStart;
      Current->PhysicalStart += MemoryMapPageCount * 4096;
    }


    Current = (EFI_MEMORY_DESCRIPTOR*)((char*)Current + EfiMemoryMap->DescriptorSize);
  }

  MEMORY_MAP MemoryMap = (MEMORY_MAP)MemoryMapAddr;
  Current = EfiMemoryMap->MemoryMap;


  int CurrentRegion = 0;
  MEMORY_TYPE LastType = MEMORY_UNKNOWN;

  for (int i = 0; i < MemoryMapEntryCount; i++)
  {
    if (MemoryMap[CurrentRegion].Start < 0x40000000000)
    {
      MemoryMap[CurrentRegion].Type = MEMORY_USED;
    }
    else {
      
      
      switch (Current->Type)
      {
        case EfiReservedMemoryType:
        case EfiRuntimeServicesCode:
        case EfiRuntimeServicesData:
        case EfiACPIMemoryNVS:
        case EfiMemoryMappedIO:
        case EfiMemoryMappedIOPortSpace:
        case EfiPalCode:
        case EfiPersistentMemory:
        case EfiUnusableMemory:
        case EfiACPIReclaimMemory:
          MemoryMap[CurrentRegion].Type = MEMORY_USED;
          break;
        case EfiBootServicesCode:
        case EfiBootServicesData:
        case EfiConventionalMemory:
        case EfiLoaderData:
        case EfiLoaderCode:
          MemoryMap[CurrentRegion].Type = MEMORY_FREE;
          break;
        default:
          MemoryMap[CurrentRegion].Type = MEMORY_USED;
          break;
      }
    }

    if (MemoryMap[CurrentRegion].Type == LastType)
    {
      CurrentRegion--;
      MemoryMap[CurrentRegion].PageCount += Current->NumberOfPages;
      LastType = MemoryMap[CurrentRegion].Type;
    }
    else 
    {
      MemoryMap[CurrentRegion].Start = Current->PhysicalStart;
      MemoryMap[CurrentRegion].PageCount = Current->NumberOfPages;
      LastType = MemoryMap[CurrentRegion].Type;
    }

    CurrentRegion++;
    Current = (EFI_MEMORY_DESCRIPTOR*)((char*)Current + EfiMemoryMap->DescriptorSize);
  }

  *KernelMemoryMap = MemoryMap;
  *MemoryMapRegionCount = CurrentRegion;

  return EFI_SUCCESS;
}

EFI_STATUS ExitBootServices(IN EFI_HANDLE ImageHandle,
                                   IN EFI_SYSTEM_TABLE *SystemTable,
                                   OUT MEMORY_MAP *KernelMemoryMap,
                                   OUT UINTN *RegionCount) {
  EFI_MEMORY_MAP MemoryMap;

  MemoryMap.MemoryMapSize = 0;
  MemoryMap.MemoryMap = NULL;
  MemoryMap.MapKey = 0;
  MemoryMap.DescriptorSize = 0;
  MemoryMap.DescriptorVersion = 0;

  // get required memory map buffer size
  EFI_STATUS Status = SystemTable->BootServices->GetMemoryMap(
      &MemoryMap.MemoryMapSize, MemoryMap.MemoryMap, &MemoryMap.MapKey,
      &MemoryMap.DescriptorSize, &MemoryMap.DescriptorVersion);

  if (Status != EFI_BUFFER_TOO_SMALL) {
    Print(u"Unexpected error getting memory map size!");
    return Status;
  }

  // allocate space for memory map buffer
  MemoryMap.MemoryMapSize += 2 * MemoryMap.DescriptorSize;
  Status = SystemTable->BootServices->AllocatePool(
      EfiLoaderData, MemoryMap.MemoryMapSize, (void **)&MemoryMap.MemoryMap);

  if (EFI_ERROR(Status)) {
    Print(u"Failed to allocate memory map buffer!");
    return Status;
  }

  // populate memory map buffer with memory map
  Status = SystemTable->BootServices->GetMemoryMap(
      &MemoryMap.MemoryMapSize, MemoryMap.MemoryMap, &MemoryMap.MapKey,
      &MemoryMap.DescriptorSize, &MemoryMap.DescriptorVersion);

  if (EFI_ERROR(Status)) {
    Print(u"Failed to get memory map!");
    return Status;
  }

  // exit boot services
  Status = SystemTable->BootServices->ExitBootServices(ImageHandle,
                                                       MemoryMap.MapKey);
  if (Status == EFI_INVALID_PARAMETER) {
    // Memory map changed, try again
    Status = SystemTable->BootServices->GetMemoryMap(
        &MemoryMap.MemoryMapSize, MemoryMap.MemoryMap, &MemoryMap.MapKey,
        &MemoryMap.DescriptorSize, &MemoryMap.DescriptorVersion);

    if (EFI_ERROR(Status)) {
      Print(u"Failed to get updated memory map!");
      SystemTable->BootServices->FreePool(MemoryMap.MemoryMap);
      return Status;
    }

    Status = SystemTable->BootServices->ExitBootServices(ImageHandle,
                                                         MemoryMap.MapKey);
  }

  if (EFI_ERROR(Status)) {
    Print(u"ExitBootServices failed!");
    SystemTable->BootServices->FreePool(MemoryMap.MemoryMap);
    return Status;
  }

  GetKernelMemoryMap(
   &MemoryMap,
   KernelMemoryMap,
   RegionCount,
   SystemTable
 );

  return EFI_SUCCESS;
}

