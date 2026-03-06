#ifndef __KMM_DRIVER_H__
#define __KMM_DRIVER_H__

#include "kmm.h"
#include "../module_types.h"
#include "kmm_types.h"

typedef struct kmm_driver {
	void (*fetch)(core_ops*);
	void (*start)(core_ops*);
	kmm_ops* kmm;
} kmm_driver;

#endif