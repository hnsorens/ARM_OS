#ifndef __BUS_CONTROLLER_DRIVER_H__
#define __BUS_CONTROLLER_DRIVER_H__

#include "bus_controller.h"
#include "../module_types.h"
#include "bus_controller_types.h"

typedef struct bus_controller_driver {
	void (*fetch)(core_ops*);
	void (*start)(core_ops*);
	bus_controller_ops* bus_controller;
} bus_controller_driver;

#endif