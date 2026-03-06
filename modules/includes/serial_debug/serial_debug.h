#ifndef SERIAL_DEBUG_VTABLE_H
#define SERIAL_DEBUG_VTABLE_H


#include "serial_debug_types.h"

typedef struct serial_debug_ops
{
  int (*serial_printf)(char*, ...);
} serial_debug_ops;

#endif