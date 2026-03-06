#ifndef __VMM_DRIVER_H__
#define __VMM_DRIVER_H__

#include "vmm.h"
#include "../module_types.h"
#include "vmm_types.h"

typedef struct vmm_driver {
	void (*fetch)(core_ops*);
	void (*start)(core_ops*);
	vmm_ops* vmm;
} vmm_driver;

#endif