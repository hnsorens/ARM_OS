#include "module.h"

#include "modules/bus_controller.h"
#include <stdint.h>

vtable(blk_dev_vtable_t)
start(init, virtio_blk_init)

#define VIRTIO_BLK_DEVICE_ID 2

bus_controller_vtable_t* virt_bus_controller;

typedef struct virtq_desc_t
{
  uint64_t addr;
  uint32_t len;
  uint16_t flags;
  uint16_t next;
} virtq_desc_t;

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
  bus_controller_fetch(kvtable);

  uintptr_t blk_device_base = bus_controller_find_device(VIRTIO_BLK_DEVICE_ID);

}