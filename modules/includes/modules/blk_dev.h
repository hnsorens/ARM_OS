#ifndef BLK_DEV_H
#define BLK_DEV_H
#include "module_vtables.h"

#ifndef BLK_DEV
#define BLK_DEV blk_dev
#endif

#define EXPAND(var) var
#define CONCAT(a, b) a##b
#define CONCAT_EXPAND(a, b) CONCAT(a, b)

#ifdef __MAIN__

#define __BLK_DEV__DEF(prefix) \
__attribute__((visibility("hidden"))) void* (*CONCAT_EXPAND(prefix, _read_sectors))( uint32_t, uint32_t ) = 0; \
__attribute__((visibility("hidden"))) void (*CONCAT_EXPAND(prefix, _write_sectors))( uint32_t, uint32_t, void* ) = 0; \
\
static void _blk_dev_init(kernel_vtable_t *kvtable){\
	blk_dev_vtable_t* module = (blk_dev_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_BLK_DEV);\
	CONCAT_EXPAND(prefix, _read_sectors) = module->read_sectors;\
	CONCAT_EXPAND(prefix, _write_sectors) = module->write_sectors;\
}

__BLK_DEV__DEF(BLK_DEV) 
#undef __BLK_DEV__DEF

#else

#define __BLK_DEV__DEF(prefix) \
__attribute__((visibility("hidden"))) extern void* (*CONCAT_EXPAND(prefix, _read_sectors))( uint32_t, uint32_t ); \
__attribute__((visibility("hidden"))) extern void (*CONCAT_EXPAND(prefix, _write_sectors))( uint32_t, uint32_t, void* ); \


__BLK_DEV__DEF(BLK_DEV) 
#undef __BLK_DEV__DEF

#endif
#endif