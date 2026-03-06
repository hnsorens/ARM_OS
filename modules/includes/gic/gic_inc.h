#ifndef __GIC_INC_H__
#define __GIC_INC_H__


#include "gic_driver.h"

#ifdef __MAIN__
#define GLOBAL __attribute__((visibility("hidden")))
#define END = 0;
#else
#define GLOBAL __attribute__((visibility("hidden"))) extern
#define END ;
#endif


#ifdef __MAIN__

static void gic_fetch(core_ops *ops) {
	gic_driver *driver = (gic_driver*)ops->find_module_by_type(MODULE_GIC);
}

#endif
#endif