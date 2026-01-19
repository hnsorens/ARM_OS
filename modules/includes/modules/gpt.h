#ifndef GPT_H
#define GPT_H

#include "modules/structures/gpt.h"
#include "module_vtables.h"

#ifndef GPT
#define GPT gpt
#endif

#define EXPAND(var) var
#define CONCAT(a, b) a##b
#define CONCAT_EXPAND(a, b) CONCAT(a, b)

#define GLOBAL __attribute__((visibility("hidden")))

#ifdef __MAIN__

#define __GPT__DEF(prefix) \
\
static void gpt_fetch(kernel_vtable_t *kvtable){\
	gpt_vtable_t* module = (gpt_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_GPT);\
}

__GPT__DEF(GPT) 
#undef __GPT__DEF

#else

#define __GPT__DEF(prefix) \


__GPT__DEF(GPT) 
#undef GLOBAL
#undef __GPT__DEF
#undef GPT

#endif
#endif