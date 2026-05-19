#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include "../include/type.h"

typedef enum memory_type
{
  MEMORY_UNKNOWN,
  MEMORY_FREE,
  MEMORY_USED,
} memory_type_t;

typedef struct memory_region
{
  paddr_t start;
  size_t size;
  memory_type_t memory_type;
} memory_region_t;

typedef struct boot_info 
{
    memory_region_t *memory_regions;
    unsigned long memory_map_size;
} boot_info_t;



#endif
