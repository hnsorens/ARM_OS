#ifndef IDE_H
#define IDE_H

#include "modules/structures/ide.h"
#include "modules/vtables/ide.h"

#ifndef IDE
#define IDE ide
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

GLOBAL void* (*CONCAT_EXPAND(IDE, _read_fn))( unsigned int, unsigned int ) END 
GLOBAL void (*CONCAT_EXPAND(IDE, _write_fn))( unsigned int, unsigned int, void* ) END 
#ifdef __MAIN__

static void ide_fetch(kernel_vtable_t *kvtable){
	ide_vtable_t* module = (ide_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_IDE);
	CONCAT_EXPAND(IDE, _read_fn) = module->read_fn;
	CONCAT_EXPAND(IDE, _write_fn) = module->write_fn;
}
#endif

#undef GLOBAL
#undef IDE

#endif