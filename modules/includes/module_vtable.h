#ifndef MODULE_VTABLE_H
#define MODULE_VTABLE_H

#include "module_types.h"
#include <stdint.h>

#define vtable_def void (*fetch)(kernel_vtable_t*, virt_addr_t load); void (*init)(kernel_vtable_t*);

#endif