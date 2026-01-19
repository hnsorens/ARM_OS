#ifndef LIBK_H
#define LIBK_H

#include "modules/structures/libk.h"
#include "module_vtables.h"

#ifndef LIBK
#define LIBK libk
#endif

#define EXPAND(var) var
#define CONCAT(a, b) a##b
#define CONCAT_EXPAND(a, b) CONCAT(a, b)

#define GLOBAL __attribute__((visibility("hidden")))

#ifdef __MAIN__

#define __LIBK__DEF(prefix) \
\
static void libk_fetch(kernel_vtable_t *kvtable){\
	libk_vtable_t* module = (libk_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_LIBK);\
}

__LIBK__DEF(LIBK) 
#undef __LIBK__DEF

#else

#define __LIBK__DEF(prefix) \


__LIBK__DEF(LIBK) 
#undef GLOBAL
#undef __LIBK__DEF
#undef LIBK

#endif
#endif