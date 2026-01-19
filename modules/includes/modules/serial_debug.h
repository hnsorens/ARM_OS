#ifndef SERIAL_DEBUG_H
#define SERIAL_DEBUG_H

#include "modules/structures/serial_debug.h"
#include "module_vtables.h"

#ifndef SERIAL_DEBUG
#define SERIAL_DEBUG serial_debug
#endif

#define EXPAND(var) var
#define CONCAT(a, b) a##b
#define CONCAT_EXPAND(a, b) CONCAT(a, b)

#define GLOBAL __attribute__((visibility("hidden")))

#ifdef __MAIN__

#define __SERIAL_DEBUG__DEF(prefix) \
GLOBAL int (*CONCAT_EXPAND(prefix, _serial_printf))( char*, ... ) = 0; \
\
static void serial_debug_fetch(kernel_vtable_t *kvtable){\
	serial_debug_vtable_t* module = (serial_debug_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_SERIAL_DEBUG);\
	CONCAT_EXPAND(prefix, _serial_printf) = module->serial_printf;\
}

__SERIAL_DEBUG__DEF(SERIAL_DEBUG) 
#undef __SERIAL_DEBUG__DEF

#else

#define __SERIAL_DEBUG__DEF(prefix) \
GLOBAL extern int (*CONCAT_EXPAND(prefix, _serial_printf))( char*, ... ); \


__SERIAL_DEBUG__DEF(SERIAL_DEBUG) 
#undef GLOBAL
#undef __SERIAL_DEBUG__DEF
#undef SERIAL_DEBUG

#endif
#endif