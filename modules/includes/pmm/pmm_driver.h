#ifndef __PMM_DRIVER_H__
#define __PMM_DRIVER_H__

#include "pmm.h"
#include "../module_types.h"
#include "pmm_types.h"

typedef struct pmm_driver {
	void (*fetch)(core_ops*);
	void (*start)(core_ops*);
	pmm_ops* pmm;
} pmm_driver;

#endif