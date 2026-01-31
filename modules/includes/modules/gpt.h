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

#ifdef __MAIN__
#define GLOBAL __attribute__((visibility("hidden")))
#define END = 0;
#else
#define GLOBAL __attribute__((visibility("hidden"))) extern 
#define END ;
#endif

#ifdef __MAIN__

static void gpt_fetch(kernel_vtable_t *kvtable){
	gpt_vtable_t* module = (gpt_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_GPT);
}
#endif

#undef GLOBAL
#undef GPT

#endif