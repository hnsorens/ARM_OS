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

typedef struct BootInfoStruct
{
    MemoryRegion *memoryRegions;
    unsigned long memoryMapSize;
} BootInfoStruct;

#endif
