#ifndef SERIAL_DEBUG_VTABLE_H
#define SERIAL_DEBUG_VTABLE_H

#include "../../module_vtable.h"

typedef struct serial_debug_vtable_t
{
  vtable_def
  int (*serial_printf)(char*, ...);
} serial_debug_vtable_t;

#endif