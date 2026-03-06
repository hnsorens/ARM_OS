#ifndef __BLK_DEV_IMPL_T__
#define __BLK_DEV_IMPL_T__

#include "blk_dev_driver.h"
#include "blk_dev.h"

#define __MODULE_NAME__ BLK_DEV
#define __MODULE_NAME_STR__ "BLK_DEV"
#define __MAIN__

blk_dev_ops __blk_dev__;
blk_dev_driver __blk_dev_ops__;

unsigned long __load_offset__;

void blk_dev_init(blk_dev_ops* blk_dev);
void blk_dev_fetch(core_ops* ops);
void blk_dev_start(core_ops* ops);

__attribute__((section(".text._entry")))
blk_dev_driver *_entry(unsigned long offset) {
	__load_offset__ = offset;

	blk_dev_init(&__blk_dev__);
	__blk_dev_ops__.blk_dev = &__blk_dev__;


	__blk_dev_ops__.start = blk_dev_start;
	__blk_dev_ops__.fetch = blk_dev_fetch;

	return &__blk_dev_ops__;
}

#endif