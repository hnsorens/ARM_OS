#ifndef __BUS_CONTROLLER_INC_H__
#define __BUS_CONTROLLER_INC_H__


#include "bus_controller_types.h"

#ifndef CONCAT_HIDDEN
#define CONCAT_HIDDEN(a, b) a ## b
#define CONCAT(a, b) CONCAT_HIDDEN(a, b)
#endif

#define bus_controller_find_device CONCAT(BUS_CONTROLLER_NAME, _find_device_func)
#define bus_controller_init_device CONCAT(BUS_CONTROLLER_NAME, _init_device_func)
#define bus_controller_setup_queue CONCAT(BUS_CONTROLLER_NAME, _setup_queue_func)
#define bus_controller_submit_request CONCAT(BUS_CONTROLLER_NAME, _submit_request_func)

extern uintptr_t bus_controller_find_device( unsigned char id );
extern void bus_controller_init_device( void* device_base );
extern int bus_controller_setup_queue( void* base, uint32_t queue_idx, void* queue );
extern int bus_controller_submit_request( void* base, uint32_t type, void* queue, void* req, unsigned long req_len, void* data, unsigned long data_len, uint8_t* status );
#endif