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

typedef struct MEMORY_REGION
{
  PHYSICAL_ADDRESS Start;
  UINTN PageCount;
  enum
  {
    MEMORY_FREE,
    MEMORY_USED,
  } Type;
} MEMORY_REGION;

typedef MEMORY_REGION *MEMORY_MAP;

typedef struct KERNEL_ENTRY
{
  MODULE_TABLE ModuleTable;
  MEMORY_MAP MemoryMap;
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

typedef struct EFI_MEMORY_MAP {
  UINTN MemoryMapSize;
  EFI_MEMORY_DESCRIPTOR *MemoryMap;
  UINTN MapKey;
  UINTN DescriptorSize;
  UINT32 DescriptorVersion;
} EFI_MEMORY_MAP;

static EFI_STATUS GetKernelMemoryMap(
  IN  EFI_SYSTEM_TABLE  *SystemTable,
  IN  EFI_MEMORY_MAP    *EfiMemoryMap,
  OUT MEMORY_MAP        *KernelMemoryMap
)
{
  UINTN MemoryMapEntryCount = EfiMemoryMap->MemoryMapSize / EfiMemoryMap->DescriptorSize;
  
  PHYSICAL_ADDRESS MemoryMapAddr;

  
  EFI_MEMORY_DESCRIPTOR* current = EfiMemoryMap->MemoryMap;

  UINTN MemoryMapPageCount = 1;
  for (int i = 0; i < MemoryMapEntryCount; i++)
  {
    if (current->Type == EfiConventionalMemory && current->NumberOfPages >= MemoryMapPageCount)
    {
      current->NumberOfPages -= MemoryMapPageCount;
      MemoryMapAddr = current->PhysicalStart;
      current->PhysicalStart += MemoryMapPageCount * 4096;
    }


    current = (EFI_MEMORY_DESCRIPTOR*)((char*)current + EfiMemoryMap->DescriptorSize);
  }

  MEMORY_MAP MemoryMap = (MEMORY_MAP)MemoryMapAddr;
  current = EfiMemoryMap->MemoryMap;

  for (int i = 0; i < MemoryMapEntryCount; i++)
  {
    MemoryMap[i].Start = current->PhysicalStart;
    MemoryMap[i].PageCount = current->NumberOfPages;

    switch (current->Type)
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
        MemoryMap[i].Type = MEMORY_USED;
        break;
      case EfiBootServicesCode:
      case EfiBootServicesData:
      case EfiLoaderData:
      case EfiLoaderCode:
      case EfiConventionalMemory:
        MemoryMap[i].Type = MEMORY_FREE;
        break;
      default:
        MemoryMap[i].Type = MEMORY_USED;
        break;
    }


    current = (EFI_MEMORY_DESCRIPTOR*)((char*)current + EfiMemoryMap->DescriptorSize);
  }
  
  *KernelMemoryMap = MemoryMap;

  return EFI_SUCCESS;
}

[[gnu::unused]]
static EFI_STATUS ExitBootServices(IN EFI_HANDLE ImageHandle,
                                   IN EFI_SYSTEM_TABLE *SystemTable,
                                   IN MEMORY_MAP *KernelMemoryMap) {
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

  GetKernelMemoryMap(
    SystemTable,
    &MemoryMap,
    KernelMemoryMap
  );

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

    GetKernelMemoryMap(
      SystemTable,
      &MemoryMap,
      KernelMemoryMap
    );

    Status = SystemTable->BootServices->ExitBootServices(ImageHandle,
                                                         MemoryMap.MapKey);
  }

  if (EFI_ERROR(Status)) {
    PRINT(u"ExitBootServices failed!");
    SystemTable->BootServices->FreePool(MemoryMap.MemoryMap);
    return Status;
  }

  return EFI_SUCCESS;
}



EFI_STATUS EFIAPI _ModuleEntryPoint(IN EFI_HANDLE ImageHandle,
                                    IN EFI_SYSTEM_TABLE *SystemTable) {

  EFI_STATUS Status;


  MODULE_TABLE ModuleTable;
  Status = LoadModules(SystemTable, &ModuleTable);
  MODULE* Modules = ModuleTable.Modules;

  MEMORY_MAP MemoryMap;
  ExitBootServices(ImageHandle, SystemTable, &MemoryMap);

  for (int i = 0; i < ModuleTable.ModuleCount; i++) {
    VOID *ModuleEntry = (VOID *)Modules[i].ModuleBase;
    Modules[i].VTable = ((UINTN(*)(VOID))ModuleEntry)();
  }

  KERNEL_ENTRY KernelEntry;
  KernelEntry.MemoryMap = MemoryMap;
  KernelEntry.ModuleTable = ModuleTable;
  KernelEntry.TotalMemory = 0;
  KernelEntry.RuntimeServices = SystemTable->RuntimeServices;

  ((KERNEL_CORE_VTABLE*)Modules[0].VTable)->kernel_entry(KernelEntry);

  return EFI_SUCCESS;
}
