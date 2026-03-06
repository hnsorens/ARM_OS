#ifndef __BLK_DEV_OPS_H__
#define __BLK_DEV_OPS_H__

#include "blk_dev.h"
#include "../module_types.h"
#include "blk_dev_types.h"

typedef struct blk_dev_driver {
	void (*fetch)(kernel_ops*);
	void (*start)(kernel_ops*);
	blk_dev_ops* blk_dev;
} blk_dev_driver;

#endif