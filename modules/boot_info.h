#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include "api/vmm.h"
#include "vmm/vmm.h"
#include <stdint.h>
#include <type.h>

typedef enum memory_type
{
  MEMORY_UNKNOWN,
  MEMORY_FREE,
  MEMORY_USED,
} memory_type_t;

typedef struct memory_region
{
  u64 start;
  u64 size;
  memory_type_t memory_type;
} memory_region_t;

typedef struct boot_info 
{
    memory_region_t *memory_regions;
    uint64_t memory_map_size;
    uint64_t virtual_start;
    //boot_region_t virtual_regions[2];
} boot_info_t;



#endif
