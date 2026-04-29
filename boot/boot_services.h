#ifndef BOOT_SERVICES_H
#define BOOT_SERVICES_H

#include <efi.h>
#include <efilib.h>
#include <efiprot.h>

typedef UINT64 PHYSICAL_ADDRESS;

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

typedef struct EFI_MEMORY_MAP {
  UINTN MemoryMapSize;
  EFI_MEMORY_DESCRIPTOR *MemoryMap;
  UINTN MapKey;
  UINTN DescriptorSize;
  UINT32 DescriptorVersion;
} EFI_MEMORY_MAP;


EFI_STATUS ExitBootServices(IN EFI_HANDLE ImageHandle,
                                   IN EFI_SYSTEM_TABLE *SystemTable,
                                   OUT MEMORY_MAP *KernelMemoryMap,
                                   OUT UINTN *RegionCount);

#endif
