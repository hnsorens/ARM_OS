#ifndef GPT_H
#define GPT_H
#include "module_vtables.h"

#ifndef GPT
#define GPT gpt
#endif

#define EXPAND(var) var
#define CONCAT(a, b) a##b
#define CONCAT_EXPAND(a, b) CONCAT(a, b)

#ifdef __MAIN__

#define __GPT__DEF(prefix) \
\
static void _gpt_init(kernel_vtable_t *kvtable){\
	gpt_vtable_t* module = (gpt_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_GPT);\
}

__GPT__DEF(GPT) 
#undef __GPT__DEF

#else

#define __GPT__DEF(prefix) \


__GPT__DEF(GPT) 
#undef __GPT__DEF

#endif
#endif