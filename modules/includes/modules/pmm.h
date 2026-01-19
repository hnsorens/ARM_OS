#ifndef PMM_H
#define PMM_H

#include "modules/structures/pmm.h"
#include "module_vtables.h"

#ifndef PMM
#define PMM pmm
#endif

#define EXPAND(var) var
#define CONCAT(a, b) a##b
#define CONCAT_EXPAND(a, b) CONCAT(a, b)

#define GLOBAL __attribute__((visibility("hidden")))

#ifdef __MAIN__

#define __PMM__DEF(prefix) \
GLOBAL void* (*CONCAT_EXPAND(prefix, _alloc_virt_kernel))( unsigned long, unsigned long ) = 0; \
GLOBAL void (*CONCAT_EXPAND(prefix, _free_virt_kernel))( unsigned long, unsigned long ) = 0; \
GLOBAL void* (*CONCAT_EXPAND(prefix, _alloc_phys))( unsigned long ) = 0; \
GLOBAL void (*CONCAT_EXPAND(prefix, _free_phys))( unsigned long, unsigned long ) = 0; \
GLOBAL unsigned long (*CONCAT_EXPAND(prefix, _memory_available))( void ) = 0; \
\
static void pmm_fetch(kernel_vtable_t *kvtable){\
	pmm_vtable_t* module = (pmm_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_PMM);\
	CONCAT_EXPAND(prefix, _alloc_virt_kernel) = module->alloc_virt_kernel;\
	CONCAT_EXPAND(prefix, _free_virt_kernel) = module->free_virt_kernel;\
	CONCAT_EXPAND(prefix, _alloc_phys) = module->alloc_phys;\
	CONCAT_EXPAND(prefix, _free_phys) = module->free_phys;\
	CONCAT_EXPAND(prefix, _memory_available) = module->memory_available;\
}

__PMM__DEF(PMM) 
#undef __PMM__DEF

#else

#define __PMM__DEF(prefix) \
GLOBAL extern void* (*CONCAT_EXPAND(prefix, _alloc_virt_kernel))( unsigned long, unsigned long ); \
GLOBAL extern void (*CONCAT_EXPAND(prefix, _free_virt_kernel))( unsigned long, unsigned long ); \
GLOBAL extern void* (*CONCAT_EXPAND(prefix, _alloc_phys))( unsigned long ); \
GLOBAL extern void (*CONCAT_EXPAND(prefix, _free_phys))( unsigned long, unsigned long ); \
GLOBAL extern unsigned long (*CONCAT_EXPAND(prefix, _memory_available))( void ); \


__PMM__DEF(PMM) 
#undef GLOBAL
#undef __PMM__DEF
#undef PMM

#endif
#endif