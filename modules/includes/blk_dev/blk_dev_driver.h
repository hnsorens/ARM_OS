#ifndef __BLK_DEV_DRIVER_H__
#define __BLK_DEV_DRIVER_H__

#include "blk_dev.h"
#include "../module_types.h"
#include "blk_dev_types.h"

typedef struct blk_dev_driver {
	void (*fetch)(core_ops*);
	void (*start)(core_ops*);
	blk_dev_ops* blk_dev;
} blk_dev_driver;

#endif