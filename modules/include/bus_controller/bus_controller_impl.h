#ifndef __BUS_CONTROLLER_INC_H__
#define __BUS_CONTROLLER_INC_H__


#include "bus_controller_types.h"

#ifndef CONCAT_HIDDEN
#define CONCAT_HIDDEN(a, b) a ## b
#define CONCAT(a, b) CONCAT_HIDDEN(a, b)
#endif

#define bus_controller_find_device CONCAT(IMPL_NAME, _find_device_func)
#define bus_controller_init_device CONCAT(IMPL_NAME, _init_device_func)
#define bus_controller_setup_queue CONCAT(IMPL_NAME, _setup_queue_func)
#define bus_controller_submit_request CONCAT(IMPL_NAME, _submit_request_func)

__attribute__((used)) uintptr_t bus_controller_find_device( unsigned char id );
__attribute__((used)) void bus_controller_init_device( void* device_base );
__attribute__((used)) int bus_controller_setup_queue( void* base, uint32_t queue_idx, void* queue );
__attribute__((used)) int bus_controller_submit_request( void* base, uint32_t type, void* queue, void* req, unsigned long req_len, void* data, unsigned long data_len, uint8_t* status );
typedef void (*init_fn_t)(void);
#define __init_func __attribute__((section(".init_array"), used))
#define MODULE_INIT(func) \
static init_fn_t __init_ptr##func __init_func = func;
#endif