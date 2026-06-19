#ifndef BOOT_INFO_H
#define BOOT_INFO_H

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

enum vmm_region_type {
    VMM_REGION_FREE    = 0,
    VMM_REGION_CODE    = 1,
    VMM_REGION_DATA    = 2,
    VMM_REGION_STACK   = 3,
    VMM_REGION_HEAP    = 4,
    VMM_REGION_MMIO    = 5,
    VMM_REGION_GUARD   = 6
};

typedef struct VirtualMemoryRegion {
    uint64_t base;
    uint64_t size;
    uint32_t flags;
    uint32_t type;
} VirtualMemoryRegion;

typedef struct BootInfoStruct
{
    MemoryRegion *memoryRegions;
    unsigned long memoryMapSize;
    VirtualMemoryRegion virtualRegion[2];
} BootInfoStruct;

#endif
