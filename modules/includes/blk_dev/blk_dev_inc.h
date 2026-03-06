#ifndef __BLK_DEV_INC_H__
#define __BLK_DEV_INC_H__


#include "blk_dev_driver.h"

#ifdef __MAIN__
#define GLOBAL __attribute__((visibility("hidden")))
#define END = 0;
#else
#define GLOBAL __attribute__((visibility("hidden"))) extern
#define END ;
#endif

GLOBAL blk_device_t (*blk_dev_create)( void* mmio_base ) END 
GLOBAL int (*blk_dev_read_sectors)( blk_device_t dev, uint64_t sector, void* buffer, uint64_t sector_count ) END 
GLOBAL int (*blk_dev_write_sector)( blk_device_t dev, uint64_t sector, const void* buffer, uint64_t sector_count ) END 
GLOBAL int (*blk_dev_flush)( void* dev ) END 

#ifdef __MAIN__

static void blk_dev_fetch(core_ops *ops) {
	blk_dev_driver *driver = (blk_dev_driver*)ops->find_module_by_type(MODULE_BLK_DEV);
blk_dev_create = driver->blk_dev->create;
blk_dev_read_sectors = driver->blk_dev->read_sectors;
blk_dev_write_sector = driver->blk_dev->write_sector;
blk_dev_flush = driver->blk_dev->flush;
}

#endif
#endif