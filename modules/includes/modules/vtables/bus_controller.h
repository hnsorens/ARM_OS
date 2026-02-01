#ifndef BUS_CONTROLLER_VTABLE_H
#define BUS_CONTROLLER_VTABLE_H

#include "../../module_vtable.h"
#include "../structures/bus_controller.h"

typedef struct bus_controller_vtable_t
{
  vtable_def
  uintptr_t (*find_device)(unsigned char id);
  void (*init_device)(void* device_base);
  int (*setup_queue)(void* base, uint32_t queue_idx, void* queue);
  int (*submit_request)(void* base, uint32_t type, void* queue, void* req, unsigned long req_len, void* data, unsigned long data_len, uint8_t* status);
} bus_controller_vtable_t;

#endif