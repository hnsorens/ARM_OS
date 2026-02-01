#ifndef BLK_DEV_VTABLE_H
#define BLK_DEV_VTABLE_H

#include "../../module_vtable.h"
#include "../structures/blk_dev.h"

typedef struct blk_dev_vtable_t
{
  vtable_def
  void* (*create)(void* dev);
  int (*read_sectors)(void *dev, uint64_t sector, void *buffer);
  int (*write_sector)(void *dev, uint64_t sector, const void *buffer);
  int (*flush)(void *dev);
} blk_dev_vtable_t;

#endif