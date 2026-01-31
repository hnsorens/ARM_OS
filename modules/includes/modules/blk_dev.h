#ifndef BLK_DEV_H
#define BLK_DEV_H

#include "modules/structures/blk_dev.h"
#include "module_vtables.h"

#ifndef BLK_DEV
#define BLK_DEV blk_dev
#endif

#define EXPAND(var) var
#define CONCAT(a, b) a##b
#define CONCAT_EXPAND(a, b) CONCAT(a, b)

#ifdef __MAIN__
#define GLOBAL __attribute__((visibility("hidden")))
#define END = 0;
#else
#define GLOBAL __attribute__((visibility("hidden"))) extern 
#define END ;
#endif

GLOBAL void* (*CONCAT_EXPAND(BLK_DEV, _create))( void* dev ) END 
GLOBAL int (*CONCAT_EXPAND(BLK_DEV, _read_sectors))( void* dev, uint64_t sector, void* buffer ) END 
GLOBAL int (*CONCAT_EXPAND(BLK_DEV, _write_sector))( void* dev, uint64_t sector, const void* buffer ) END 
GLOBAL int (*CONCAT_EXPAND(BLK_DEV, _flush))( void* dev ) END 
#ifdef __MAIN__

static void blk_dev_fetch(kernel_vtable_t *kvtable){
	blk_dev_vtable_t* module = (blk_dev_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_BLK_DEV);
	CONCAT_EXPAND(BLK_DEV, _create) = module->create;
	CONCAT_EXPAND(BLK_DEV, _read_sectors) = module->read_sectors;
	CONCAT_EXPAND(BLK_DEV, _write_sector) = module->write_sector;
	CONCAT_EXPAND(BLK_DEV, _flush) = module->flush;
}
#endif

#undef GLOBAL
#undef BLK_DEV

#endif