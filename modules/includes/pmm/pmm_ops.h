#ifndef __PMM_OPS_H__
#define __PMM_OPS_H__

#include "pmm.h"
#include "../module_types.h"
#include "pmm_types.h"

typedef struct pmm_driver {
	void (*fetch)(kernel_ops*);
	void (*start)(kernel_ops*);
	pmm_ops* pmm;
} pmm_driver;

#endif