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

#ifdef __MAIN__

#define __KMM__DEF(prefix) \
__attribute__((visibility("hidden"))) virt_addr_t (*CONCAT_EXPAND(prefix, _kmalloc))( unsigned long ) = 0; \
__attribute__((visibility("hidden"))) virt_addr_t (*CONCAT_EXPAND(prefix, _kcalloc))( unsigned long, unsigned long ) = 0; \
__attribute__((visibility("hidden"))) virt_addr_t (*CONCAT_EXPAND(prefix, _krealloc))( virt_addr_t, unsigned long ) = 0; \
__attribute__((visibility("hidden"))) void (*CONCAT_EXPAND(prefix, _kfree))( virt_addr_t ) = 0; \
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
__attribute__((visibility("hidden"))) extern virt_addr_t (*CONCAT_EXPAND(prefix, _kmalloc))( unsigned long ); \
__attribute__((visibility("hidden"))) extern virt_addr_t (*CONCAT_EXPAND(prefix, _kcalloc))( unsigned long, unsigned long ); \
__attribute__((visibility("hidden"))) extern virt_addr_t (*CONCAT_EXPAND(prefix, _krealloc))( virt_addr_t, unsigned long ); \
__attribute__((visibility("hidden"))) extern void (*CONCAT_EXPAND(prefix, _kfree))( virt_addr_t ); \


__KMM__DEF(KMM) 
#undef __KMM__DEF

#endif
#endif