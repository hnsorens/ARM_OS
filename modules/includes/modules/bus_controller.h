#ifndef BUS_CONTROLLER_H
#define BUS_CONTROLLER_H

#include "modules/structures/bus_controller.h"
#include "module_vtables.h"

#ifndef BUS_CONTROLLER
#define BUS_CONTROLLER bus_controller
#endif

#define EXPAND(var) var
#define CONCAT(a, b) a##b
#define CONCAT_EXPAND(a, b) CONCAT(a, b)

#define GLOBAL __attribute__((visibility("hidden")))

#ifdef __MAIN__

#define __BUS_CONTROLLER__DEF(prefix) \
GLOBAL uintptr_t (*CONCAT_EXPAND(prefix, _find_device))( unsigned char ) = 0; \
GLOBAL void (*CONCAT_EXPAND(prefix, _init_device))( unsigned long ) = 0; \
\
static void bus_controller_fetch(kernel_vtable_t *kvtable){\
	bus_controller_vtable_t* module = (bus_controller_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_BUS_CONTROLLER);\
	CONCAT_EXPAND(prefix, _find_device) = module->find_device;\
	CONCAT_EXPAND(prefix, _init_device) = module->init_device;\
}

__BUS_CONTROLLER__DEF(BUS_CONTROLLER) 
#undef __BUS_CONTROLLER__DEF

#else

#define __BUS_CONTROLLER__DEF(prefix) \
GLOBAL extern uintptr_t (*CONCAT_EXPAND(prefix, _find_device))( unsigned char ); \
GLOBAL extern void (*CONCAT_EXPAND(prefix, _init_device))( unsigned long ); \


__BUS_CONTROLLER__DEF(BUS_CONTROLLER) 
#undef GLOBAL
#undef __BUS_CONTROLLER__DEF
#undef BUS_CONTROLLER

#endif
#endif