#ifndef BUS_CONTROLLER_H
#define BUS_CONTROLLER_H

#include "modules/structures/bus_controller.h"
#include "modules/vtables/bus_controller.h"

#ifndef BUS_CONTROLLER
#define BUS_CONTROLLER bus_controller
#endif

#define EXPAND(var) var
#define CONCAT(a, b) a##b
#define CONCAT_EXPAND(a, b) CONCAT(a, b)

#ifdef __MAIN__
#define GLOBAL __attribute__((visibility("hidden")))
#define END = 0;
#else
#define GLOBAL __attribute__((visibility("hidden"))) extern 
#define END ;
#endif

GLOBAL uintptr_t (*CONCAT_EXPAND(BUS_CONTROLLER, _find_device))( unsigned char id ) END 
GLOBAL void (*CONCAT_EXPAND(BUS_CONTROLLER, _init_device))( void* device_base ) END 
GLOBAL int (*CONCAT_EXPAND(BUS_CONTROLLER, _setup_queue))( void* base, uint32_t queue_idx, void* queue ) END 
GLOBAL int (*CONCAT_EXPAND(BUS_CONTROLLER, _submit_request))( void* base, uint32_t type, void* queue, void* req, unsigned long req_len, void* data, unsigned long data_len, uint8_t* status ) END 
#ifdef __MAIN__

static void bus_controller_fetch(kernel_vtable_t *kvtable){
	bus_controller_vtable_t* module = (bus_controller_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_BUS_CONTROLLER);
	CONCAT_EXPAND(BUS_CONTROLLER, _find_device) = module->find_device;
	CONCAT_EXPAND(BUS_CONTROLLER, _init_device) = module->init_device;
	CONCAT_EXPAND(BUS_CONTROLLER, _setup_queue) = module->setup_queue;
	CONCAT_EXPAND(BUS_CONTROLLER, _submit_request) = module->submit_request;
}
#endif

#undef GLOBAL
#undef BUS_CONTROLLER

#endif