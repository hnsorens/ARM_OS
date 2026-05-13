#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include "utils.h"

typedef enum memory_type
{
  MEMORY_UNKNOWN,
  MEMORY_FREE,
  MEMORY_USED,
} memory_type_t;

typedef struct memory_region
{
  void *start;
  size_t size;
  memory_type_t memory_type;
} memory_region_t;



#endif
