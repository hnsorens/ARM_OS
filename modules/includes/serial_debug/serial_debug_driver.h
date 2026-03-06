#ifndef __SERIAL_DEBUG_DRIVER_H__
#define __SERIAL_DEBUG_DRIVER_H__

#include "serial_debug.h"
#include "../module_types.h"
#include "serial_debug_types.h"

typedef struct serial_debug_driver {
	void (*fetch)(core_ops*);
	void (*start)(core_ops*);
	serial_debug_ops* serial_debug;
} serial_debug_driver;

#endif