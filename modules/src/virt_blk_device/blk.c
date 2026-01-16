#include "module.h"

vtable(blk_dev_vtable_t)
start(init, virtio_blk_init)

bus_controller_vtable_t* virt_bus_controller;

void read_sector()
{

}

void write_sector()
{

}

void create_device()
{
  unsigned long device = virt_bus_controller->find_device(0x02);
}

void init(blk_dev_vtable_t *vtable)
{

}

void virtio_blk_init(kernel_vtable_t* kvtable)
{
  virt_bus_controller = (bus_controller_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_BUS_CONTROLLER);
}