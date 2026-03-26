#ifndef __SERIAL_DEBUG_INC_H__
#define __SERIAL_DEBUG_INC_H__


#include "serial_debug_types.h"

#ifndef CONCAT_HIDDEN
#define CONCAT_HIDDEN(a, b) a ## b
#define CONCAT(a, b) CONCAT_HIDDEN(a, b)
#endif

#define serial_debug_serial_printf CONCAT(SERIAL_DEBUG_NAME, _serial_printf_func)

extern int serial_debug_serial_printf( char*, ... );
#endif