#include "Base.h"
#include "Guid/FileInfo.h"
#include "ProcessorBind.h"
#include "Protocol/SimpleFileSystem.h"
#include "Uefi/UefiBaseType.h"

#include "Uefi/UefiMultiPhase.h"
#include "Uefi/UefiSpec.h"

#include <Uefi.h>
#include <stdint.h>

#include "module_loader.h"

#define DEBUG



#ifdef DEBUG

#define BREAK while(1);
#define LOG(reg, val) __asm__ volatile("mov " #reg ", %0" :: "r"((unsigned long)val));

#define DISPLAY_MEMORY_MAP
#define PRINT_EFI_MEMORY_MAP
#define PRINT_EFI_MEMORY_MAP_ENTRY_COUNT
#define PRINT_EFI_MEMORY_MAP_PHYS_ADDR
#define PRINT_KERNEL_MEMORY_MAP_PHYS_ADDR

#define PRINT_VALUE(string, value)  \
PRINT(L##string);                   \
PRINT(L": ");                       \
PrintNumber(SystemTable, value);    \
PRINT(L"\n"); 
#else
#define BREAK
#define LOG(reg, val)
#endif

typedef enum MEMORY_TYPE
{
  MEMORY_UNKNOWN,
  MEMORY_FREE,
  MEMORY_USED,
} MEMORY_TYPE;

typedef struct MEMORY_REGION
{
  PHYSICAL_ADDRESS Start;
  UINTN PageCount;
  MEMORY_TYPE Type;
} MEMORY_REGION;

typedef MEMORY_REGION *MEMORY_MAP;

typedef struct KERNEL_ENTRY
{
  MODULE_TABLE ModuleTable;
  MEMORY_MAP MemoryMap;
  UINTN RegionCount;
  UINTN TotalMemory;
  EFI_RUNTIME_SERVICES *RuntimeServices;
} KERNEL_ENTRY;

typedef struct KERNEL_CORE_VTABLE
{
  void (*kernel_entry)(KERNEL_ENTRY);
} KERNEL_CORE_VTABLE;

EFI_GUID gEfiSimpleFileSystemProtocolGuid = {
    0x964e5b22,
    0x6459,
    0x11d2,
    {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
EFI_GUID gEfiFileInfoGuid = {0x09576e92,
    0x6d3f,
    0x11d2,
    {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};

#ifdef DEBUG
#define PRINT(str) SystemTable->ConOut->OutputString(SystemTable->ConOut, str);
void PrintNumber(EFI_SYSTEM_TABLE *SystemTable, UINT64 number) {
  CHAR16 buffer[64];
  INTN i = 0;

  // Handle zero
  if (number == 0) {
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"0");
    return;
  }

  // Convert to string backwards
  while (number > 0) {
    buffer[i++] = L'0' + (number % 10);
    number /= 10;
  }

  // Print in correct order
  while (i > 0) {
    CHAR16 digit[2] = {buffer[--i], 0};
    SystemTable->ConOut->OutputString(SystemTable->ConOut, digit);
  }
}
#endif

typedef struct EFI_MEMORY_MAP {
  UINTN MemoryMapSize;
  EFI_MEMORY_DESCRIPTOR *MemoryMap;
  UINTN MapKey;
  UINTN DescriptorSize;
  UINT32 DescriptorVersion;
} EFI_MEMORY_MAP;

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
  OUT UINTN             *MemoryMapRegionCount
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
      case EfiLoaderData:
      case EfiLoaderCode:
      case EfiConventionalMemory:
        MemoryMap[CurrentRegion].Type = MEMORY_FREE;
        break;
      default:
        MemoryMap[CurrentRegion].Type = MEMORY_USED;
        break;
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

[[gnu::unused]]
static EFI_STATUS ExitBootServices(IN EFI_HANDLE ImageHandle,
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
    PRINT(u"Unexpected error getting memory map size!");
    return Status;
  }

  // allocate space for memory map buffer
  MemoryMap.MemoryMapSize += 2 * MemoryMap.DescriptorSize;
  Status = SystemTable->BootServices->AllocatePool(
      EfiLoaderData, MemoryMap.MemoryMapSize, (void **)&MemoryMap.MemoryMap);

  if (EFI_ERROR(Status)) {
    PRINT(u"Failed to allocate memory map buffer!");
    return Status;
  }

  // populate memory map buffer with memory map
  Status = SystemTable->BootServices->GetMemoryMap(
      &MemoryMap.MemoryMapSize, MemoryMap.MemoryMap, &MemoryMap.MapKey,
      &MemoryMap.DescriptorSize, &MemoryMap.DescriptorVersion);

  if (EFI_ERROR(Status)) {
    PRINT(u"Failed to get memory map!");
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
      PRINT(u"Failed to get updated memory map!");
      SystemTable->BootServices->FreePool(MemoryMap.MemoryMap);
      return Status;
    }

    Status = SystemTable->BootServices->ExitBootServices(ImageHandle,
                                                         MemoryMap.MapKey);
  }

  if (EFI_ERROR(Status)) {
    PRINT(u"ExitBootServices failed!");
    LOG(x10, Status);
    SystemTable->BootServices->FreePool(MemoryMap.MemoryMap);
    return Status;
  }

  GetKernelMemoryMap(
   &MemoryMap,
   KernelMemoryMap,
   RegionCount
 );

  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI _ModuleEntryPoint(IN EFI_HANDLE ImageHandle,
                                    IN EFI_SYSTEM_TABLE *SystemTable) {

  EFI_STATUS Status;


  MODULE_TABLE ModuleTable;
  Status = LoadModules(SystemTable, &ModuleTable);
  MODULE* Modules = ModuleTable.Modules;

  PRINT_VALUE("MODULE0", ModuleTable.Modules[0].ModuleBase);
  PRINT_VALUE("MODULE1", ModuleTable.Modules[1].ModuleBase);
  PRINT_VALUE("MODULE2", ModuleTable.Modules[2].ModuleBase);
  // BREAK

  MEMORY_MAP MemoryMap;
  UINTN MemoryMapRegionsCount;
  ExitBootServices(ImageHandle, SystemTable, &MemoryMap, &MemoryMapRegionsCount);


  for (int i = 0; i < ModuleTable.ModuleCount; i++) {
    VOID *ModuleEntry = (VOID *)Modules[i].ModuleBase;
    Modules[i].VTable = ((UINTN(*)(VOID))ModuleEntry)();
  }



  KERNEL_ENTRY KernelEntry;
  KernelEntry.MemoryMap = MemoryMap;
  KernelEntry.ModuleTable = ModuleTable;
  KernelEntry.RegionCount = MemoryMapRegionsCount;
  KernelEntry.TotalMemory = 0;
  KernelEntry.RuntimeServices = SystemTable->RuntimeServices;

  ((KERNEL_CORE_VTABLE*)Modules[0].VTable)->kernel_entry(KernelEntry);

  return EFI_SUCCESS;
}
