#ifndef __GPT_INC_H__
#define __GPT_INC_H__


#include "gpt_driver.h"

#ifdef __MAIN__
#define GLOBAL __attribute__((visibility("hidden")))
#define END = 0;
#else
#define GLOBAL __attribute__((visibility("hidden"))) extern
#define END ;
#endif

 
GLOBAL gpt_partition_t* (*gpt_create)( blk_device_t dev ) END 

#ifdef __MAIN__

static void gpt_fetch(core_ops *ops) {
	gpt_driver *driver = (gpt_driver*)ops->find_module_by_type(MODULE_GPT);
gpt_create = driver->gpt->create;
}

#endif
#endif