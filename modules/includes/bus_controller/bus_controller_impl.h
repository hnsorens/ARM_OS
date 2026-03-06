#ifndef __BUS_CONTROLLER_IMPL_T__
#define __BUS_CONTROLLER_IMPL_T__

#include "bus_controller_driver.h"
#include "bus_controller.h"

#define __MODULE_NAME__ BUS_CONTROLLER
#define __MODULE_NAME_STR__ "BUS_CONTROLLER"
#define __MAIN__

bus_controller_ops __bus_controller__;
bus_controller_driver __bus_controller_ops__;

unsigned long __load_offset__;

void bus_controller_init(bus_controller_ops* bus_controller);
void bus_controller_fetch(core_ops* ops);
void bus_controller_start(core_ops* ops);

__attribute__((section(".text._entry")))
bus_controller_driver *_entry(unsigned long offset) {
	__load_offset__ = offset;

	bus_controller_init(&__bus_controller__);
	__bus_controller_ops__.bus_controller = &__bus_controller__;


	__bus_controller_ops__.start = bus_controller_start;
	__bus_controller_ops__.fetch = bus_controller_fetch;

	return &__bus_controller_ops__;
}

#endif