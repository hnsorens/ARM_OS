#ifndef IDE_H
#define IDE_H
#include "module_vtables.h"

#ifndef IDE
#define IDE ide
#endif

#define EXPAND(var) var
#define CONCAT(a, b) a##b
#define CONCAT_EXPAND(a, b) CONCAT(a, b)

#ifdef __MAIN__

#define __IDE__DEF(prefix) \
__attribute__((visibility("hidden"))) void* (*CONCAT_EXPAND(prefix, _read_fn))( unsigned int, unsigned int ) = 0; \
__attribute__((visibility("hidden"))) void (*CONCAT_EXPAND(prefix, _write_fn))( unsigned int, unsigned int, void* ) = 0; \
\
static void _ide_init(kernel_vtable_t *kvtable){\
	ide_vtable_t* module = (ide_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_IDE);\
	CONCAT_EXPAND(prefix, _read_fn) = module->read_fn;\
	CONCAT_EXPAND(prefix, _write_fn) = module->write_fn;\
}

__IDE__DEF(IDE) 
#undef __IDE__DEF

#else

#define __IDE__DEF(prefix) \
__attribute__((visibility("hidden"))) extern void* (*CONCAT_EXPAND(prefix, _read_fn))( unsigned int, unsigned int ); \
__attribute__((visibility("hidden"))) extern void (*CONCAT_EXPAND(prefix, _write_fn))( unsigned int, unsigned int, void* ); \


__IDE__DEF(IDE) 
#undef __IDE__DEF

#endif
#endif