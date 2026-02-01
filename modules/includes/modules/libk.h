#ifndef LIBK_H
#define LIBK_H

#include "modules/structures/libk.h"
#include "modules/vtables/libk.h"

#ifndef LIBK
#define LIBK libk
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

#ifdef __MAIN__

static void libk_fetch(kernel_vtable_t *kvtable){
	libk_vtable_t* module = (libk_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_LIBK);
}
#endif

#undef GLOBAL
#undef LIBK

#endif