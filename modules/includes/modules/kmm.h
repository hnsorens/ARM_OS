#ifndef KMM_H
#define KMM_H

#include "modules/structures/kmm.h"
#include "module_vtables.h"

#ifndef KMM
#define KMM kmm
#endif

#define EXPAND(var) var
#define CONCAT(a, b) a##b
#define CONCAT_EXPAND(a, b) CONCAT(a, b)

#define GLOBAL __attribute__((visibility("hidden")))

#ifdef __MAIN__

#define __KMM__DEF(prefix) \
GLOBAL void* (*CONCAT_EXPAND(prefix, _kmalloc))( unsigned long ) = 0; \
GLOBAL void* (*CONCAT_EXPAND(prefix, _kcalloc))( unsigned long, unsigned long ) = 0; \
GLOBAL void* (*CONCAT_EXPAND(prefix, _krealloc))( void*, unsigned long ) = 0; \
GLOBAL void (*CONCAT_EXPAND(prefix, _kfree))( void* ) = 0; \
\
static void kmm_fetch(kernel_vtable_t *kvtable){\
	kmm_vtable_t* module = (kmm_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_KMM);\
	CONCAT_EXPAND(prefix, _kmalloc) = module->kmalloc;\
	CONCAT_EXPAND(prefix, _kcalloc) = module->kcalloc;\
	CONCAT_EXPAND(prefix, _krealloc) = module->krealloc;\
	CONCAT_EXPAND(prefix, _kfree) = module->kfree;\
}

__KMM__DEF(KMM) 
#undef __KMM__DEF

#else

#define __KMM__DEF(prefix) \
GLOBAL extern void* (*CONCAT_EXPAND(prefix, _kmalloc))( unsigned long ); \
GLOBAL extern void* (*CONCAT_EXPAND(prefix, _kcalloc))( unsigned long, unsigned long ); \
GLOBAL extern void* (*CONCAT_EXPAND(prefix, _krealloc))( void*, unsigned long ); \
GLOBAL extern void (*CONCAT_EXPAND(prefix, _kfree))( void* ); \


__KMM__DEF(KMM) 
#undef GLOBAL
#undef __KMM__DEF
#undef KMM

#endif
#endif