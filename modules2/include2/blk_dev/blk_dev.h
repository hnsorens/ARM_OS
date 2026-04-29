#ifndef BLK_DEV_VTABLE_H
#define BLK_DEV_VTABLE_H


#include "blk_dev_types.h"
#include <stdint.h>

typedef struct blk_dev_ops
{
  blk_device_t (*create)(void* mmio_base);
    int (*read_sectors)(blk_device_t dev, uint64_t sector, void *buffer, uint64_t sector_count);
    int (*write_sector)(blk_device_t dev, uint64_t sector, const void *buffer, uint64_t sector_count);
  int (*flush)(void *dev);
} blk_dev_ops;

#endif
