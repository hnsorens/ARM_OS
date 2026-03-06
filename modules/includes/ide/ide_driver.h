#ifndef __IDE_DRIVER_H__
#define __IDE_DRIVER_H__

#include "ide.h"
#include "../module_types.h"
#include "ide_types.h"

typedef struct ide_driver {
	void (*fetch)(core_ops*);
	void (*start)(core_ops*);
	ide_ops* ide;
} ide_driver;

#endif