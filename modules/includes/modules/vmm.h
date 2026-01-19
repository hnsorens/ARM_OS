#ifndef VMM_H
#define VMM_H

#include "modules/structures/vmm.h"
#include "module_vtables.h"

#ifndef VMM
#define VMM vmm
#endif

#define EXPAND(var) var
#define CONCAT(a, b) a##b
#define CONCAT_EXPAND(a, b) CONCAT(a, b)

#define GLOBAL __attribute__((visibility("hidden")))

#ifdef __MAIN__

#define __VMM__DEF(prefix) \
GLOBAL void (*CONCAT_EXPAND(prefix, _pages_map_kernel))( unsigned long, unsigned long, unsigned long, unsigned long ) = 0; \
GLOBAL unsigned long (*CONCAT_EXPAND(prefix, _virt_to_phys_kernel))( unsigned long ) = 0; \
\
static void vmm_fetch(kernel_vtable_t *kvtable){\
	vmm_vtable_t* module = (vmm_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_VMM);\
	CONCAT_EXPAND(prefix, _pages_map_kernel) = module->pages_map_kernel;\
	CONCAT_EXPAND(prefix, _virt_to_phys_kernel) = module->virt_to_phys_kernel;\
}

__VMM__DEF(VMM) 
#undef __VMM__DEF

#else

#define __VMM__DEF(prefix) \
GLOBAL extern void (*CONCAT_EXPAND(prefix, _pages_map_kernel))( unsigned long, unsigned long, unsigned long, unsigned long ); \
GLOBAL extern unsigned long (*CONCAT_EXPAND(prefix, _virt_to_phys_kernel))( unsigned long ); \


__VMM__DEF(VMM) 
#undef GLOBAL
#undef __VMM__DEF
#undef VMM

#endif
#endif