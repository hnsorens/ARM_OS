#ifndef SERIAL_DEBUG_H
#define SERIAL_DEBUG_H

#include "modules/structures/serial_debug.h"
#include "modules/vtables/serial_debug.h"

#ifndef SERIAL_DEBUG
#define SERIAL_DEBUG serial_debug
#endif

#define EXPAND(var) var
#define CONCAT(a, b) a##b
#define CONCAT_EXPAND(a, b) CONCAT(a, b)

#ifdef __MAIN__
#define GLOBAL __attribute__((visibility("hidden")))
#define END = 0;
#else
#define GLOBAL __attribute__((visibility("hidden"))) extern 
#define END ;
#endif

GLOBAL int (*CONCAT_EXPAND(SERIAL_DEBUG, _serial_printf))( char*, ... ) END 
#ifdef __MAIN__

static void serial_debug_fetch(kernel_vtable_t *kvtable){
	serial_debug_vtable_t* module = (serial_debug_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_SERIAL_DEBUG);
	CONCAT_EXPAND(SERIAL_DEBUG, _serial_printf) = module->serial_printf;
}
#endif

#undef GLOBAL
#undef SERIAL_DEBUG

#endif