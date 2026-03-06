#ifndef __GIC_DRIVER_H__
#define __GIC_DRIVER_H__

#include "gic.h"
#include "../module_types.h"
#include "gic_types.h"

typedef struct gic_driver {
	void (*fetch)(core_ops*);
	void (*start)(core_ops*);
	gic_ops* gic;
} gic_driver;

#endif