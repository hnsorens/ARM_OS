#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include "modules/elf_loader.h"
#include <stdint.h>
typedef enum MemoryType
{
  MEMORY_UNKNOWN,
  MEMORY_FREE,
  MEMORY_USED,
} MemoryType;

typedef struct MemoryRegion
{
  uint64_t start;
  uint64_t page_count;
  MemoryType type;
} MemoryRegion;

typedef struct BootInfoStruct
{
    MemoryRegion *memoryRegions;
    UINT64 memoryMapSize;
    UINT64 virtualStart;
} BootInfoStruct;

#endif
