#ifndef __SERIAL_DEBUG_INC_H__
#define __SERIAL_DEBUG_INC_H__


#include "serial_debug_driver.h"

#ifdef __MAIN__
#define GLOBAL __attribute__((visibility("hidden")))
#define END = 0;
#else
#define GLOBAL __attribute__((visibility("hidden"))) extern
#define END ;
#endif

GLOBAL int (*serial_debug_serial_printf)( char*, ... ) END 

#ifdef __MAIN__

static void serial_debug_fetch(core_ops *ops) {
	serial_debug_driver *driver = (serial_debug_driver*)ops->find_module_by_type(MODULE_SERIAL_DEBUG);
serial_debug_serial_printf = driver->serial_debug->serial_printf;
}

#endif
#endif