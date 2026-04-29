#ifndef BOOT_H
#define BOOT_H

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


EFI_GUID gEfiSimpleFileSystemProtocolGuid = {
    0x964e5b22,
    0x6459,
    0x11d2,
    {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
EFI_GUID gEfiFileInfoGuid = {0x09576e92,
    0x6d3f,
    0x11d2,
    {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};

#endif
