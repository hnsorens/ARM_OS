#ifndef __MMU_DRIVER_H__
#define __MMU_DRIVER_H__

#include "mmu.h"
#include "../module_types.h"
#include "mmu_types.h"

typedef struct mmu_driver {
	void (*fetch)(core_ops*);
	void (*start)(core_ops*);
	mmu_ops* mmu;
} mmu_driver;

#endif