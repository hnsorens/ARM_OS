#ifndef __BUS_CONTROLLER_INC_H__
#define __BUS_CONTROLLER_INC_H__


#include "bus_controller_driver.h"

#ifdef __MAIN__
#define GLOBAL __attribute__((visibility("hidden")))
#define END = 0;
#else
#define GLOBAL __attribute__((visibility("hidden"))) extern
#define END ;
#endif

GLOBAL uintptr_t (*bus_controller_find_device)( unsigned char id ) END 
GLOBAL void (*bus_controller_init_device)( void* device_base ) END 
GLOBAL int (*bus_controller_setup_queue)( void* base, uint32_t queue_idx, void* queue ) END 
GLOBAL int (*bus_controller_submit_request)( void* base, uint32_t type, void* queue, void* req, unsigned long req_len, void* data, unsigned long data_len, uint8_t* status ) END 

#ifdef __MAIN__

static void bus_controller_fetch(core_ops *ops) {
	bus_controller_driver *driver = (bus_controller_driver*)ops->find_module_by_type(MODULE_BUS_CONTROLLER);
bus_controller_find_device = driver->bus_controller->find_device;
bus_controller_init_device = driver->bus_controller->init_device;
bus_controller_setup_queue = driver->bus_controller->setup_queue;
bus_controller_submit_request = driver->bus_controller->submit_request;
}

#endif
#endif