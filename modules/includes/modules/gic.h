#ifndef GIC_H
#define GIC_H

#include "modules/structures/gic.h"
#include "modules/vtables/gic.h"

#ifndef GIC
#define GIC gic
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

static void gic_fetch(kernel_vtable_t *kvtable){
	gic_vtable_t* module = (gic_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_GIC);
}
#endif

#undef GLOBAL
#undef GIC

#endif