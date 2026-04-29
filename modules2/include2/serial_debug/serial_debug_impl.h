#ifndef __SERIAL_DEBUG_INC_H__
#define __SERIAL_DEBUG_INC_H__


#include "serial_debug_types.h"

#ifndef CONCAT_HIDDEN
#define CONCAT_HIDDEN(a, b) a ## b
#define CONCAT(a, b) CONCAT_HIDDEN(a, b)
#endif

#define serial_debug_serial_printf CONCAT(IMPL_NAME, _serial_printf_func)

__attribute__((used)) int serial_debug_serial_printf( char*, ... );
typedef void (*init_fn_t)(void);
#define __init_func __attribute__((section(".init_array"), used))
#define MODULE_INIT(func) \
static init_fn_t __init_ptr##func __init_func = func;
#define __entry_func __attribute__((section(".entry_point"), used))
#define KERNEL_ENTRY(func) int _start() { func(); return 0; }
#endif
