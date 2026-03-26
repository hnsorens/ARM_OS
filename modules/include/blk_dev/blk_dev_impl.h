#ifndef __BLK_DEV_INC_H__
#define __BLK_DEV_INC_H__


#include "blk_dev_types.h"

#ifndef CONCAT_HIDDEN
#define CONCAT_HIDDEN(a, b) a ## b
#define CONCAT(a, b) CONCAT_HIDDEN(a, b)
#endif

#define blk_dev_create CONCAT(IMPL_NAME, _create_func)
#define blk_dev_read_sectors CONCAT(IMPL_NAME, _read_sectors_func)
#define blk_dev_write_sector CONCAT(IMPL_NAME, _write_sector_func)
#define blk_dev_flush CONCAT(IMPL_NAME, _flush_func)

__attribute__((used)) blk_device_t blk_dev_create( void* mmio_base );
__attribute__((used)) int blk_dev_read_sectors( blk_device_t dev, uint64_t sector, void* buffer, uint64_t sector_count );
__attribute__((used)) int blk_dev_write_sector( blk_device_t dev, uint64_t sector, const void* buffer, uint64_t sector_count );
__attribute__((used)) int blk_dev_flush( void* dev );
typedef void (*init_fn_t)(void);
#define __init_func __attribute__((section(".init_array"), used))
#define MODULE_INIT(func) \
static init_fn_t __init_ptr##func __init_func = func;
#endif