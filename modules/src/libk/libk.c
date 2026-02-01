#include "module.h"

#include "modules/vtables/libk.h"

#include "modules/serial_debug.h"

vtable(libk_vtable_t);
start(init, libk_fetch, libk_init);

void libk_init(kernel_vtable_t *kvtable)
{

}

void libk_fetch(kernel_vtable_t *kvtable)
{
  serial_debug_fetch(kvtable);
}

void init(libk_vtable_t *vtable)
{

}
