#ifndef BOOT_INFO_H
#define BOOT_INFO_H

typedef enum MemoryType
{
  MEMORY_UNKNOWN,
  MEMORY_FREE,
  MEMORY_USED,
} MemoryType;

typedef struct MemoryRegion
{
  unsigned long start;
  unsigned long page_count;
  MemoryType type;
} MemoryRegion;

#pragma pack(push, 1)
typedef struct BootInfoStruct
{
    unsigned long memoryMapSize;
    MemoryRegion *memoryRegions;
} BootInfoStruct;
#pragma pack(pop)

#endif
