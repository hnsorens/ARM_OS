#ifndef GIC_H
#define GIC_H
#include "modules/structures/gic.h"
#include "module_vtables.h"

#ifndef GIC
#define GIC gic
#endif

#define EXPAND(var) var
#define CONCAT(a, b) a##b
#define CONCAT_EXPAND(a, b) CONCAT(a, b)

#ifdef __MAIN__

#define __GIC__DEF(prefix) \
\
static void gic_fetch(kernel_vtable_t *kvtable){\
	gic_vtable_t* module = (gic_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_GIC);\
}

__GIC__DEF(GIC) 
#undef __GIC__DEF

#else

#define __GIC__DEF(prefix) \


__GIC__DEF(GIC) 
#undef __GIC__DEF

#endif
#endif