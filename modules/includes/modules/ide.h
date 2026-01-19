#ifndef IDE_H
#define IDE_H

#include "modules/structures/ide.h"
#include "module_vtables.h"

#ifndef IDE
#define IDE ide
#endif

#define EXPAND(var) var
#define CONCAT(a, b) a##b
#define CONCAT_EXPAND(a, b) CONCAT(a, b)

#define GLOBAL __attribute__((visibility("hidden")))

#ifdef __MAIN__

#define __IDE__DEF(prefix) \
GLOBAL void* (*CONCAT_EXPAND(prefix, _read_fn))( unsigned int, unsigned int ) = 0; \
GLOBAL void (*CONCAT_EXPAND(prefix, _write_fn))( unsigned int, unsigned int, void* ) = 0; \
\
static void ide_fetch(kernel_vtable_t *kvtable){\
	ide_vtable_t* module = (ide_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_IDE);\
	CONCAT_EXPAND(prefix, _read_fn) = module->read_fn;\
	CONCAT_EXPAND(prefix, _write_fn) = module->write_fn;\
}

__IDE__DEF(IDE) 
#undef __IDE__DEF

#else

#define __IDE__DEF(prefix) \
GLOBAL extern void* (*CONCAT_EXPAND(prefix, _read_fn))( unsigned int, unsigned int ); \
GLOBAL extern void (*CONCAT_EXPAND(prefix, _write_fn))( unsigned int, unsigned int, void* ); \


__IDE__DEF(IDE) 
#undef GLOBAL
#undef __IDE__DEF
#undef IDE

#endif
#endif