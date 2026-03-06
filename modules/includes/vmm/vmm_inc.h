#ifndef __VMM_INC_H__
#define __VMM_INC_H__


#include "vmm_driver.h"

#ifdef __MAIN__
#define GLOBAL __attribute__((visibility("hidden")))
#define END = 0;
#else
#define GLOBAL __attribute__((visibility("hidden"))) extern
#define END ;
#endif

GLOBAL vaddr_t (*vmm_alloc)( vmm_context* ctx, size_t sz, flags_t flags ) END 
 
    // The "Cleanup": Unmap from MMU, tell PMM to free the frames. 
GLOBAL int (*vmm_free)( vmm_context* ctx, vaddr_t v, size_t sz ) END 
 
    // The "Translator": Virtual to Physical (usually a walk of the MMU tables). 
GLOBAL paddr_t (*vmm_v2p)( vmm_context* ctx, vaddr_t va ) END 
 
    // The "Emergency": What to do when a CPU hits an unmapped address. 
GLOBAL int (*vmm_handle_fault)( vmm_context* ctx, vaddr_t addr, err_t err ) END 

#ifdef __MAIN__

static void vmm_fetch(core_ops *ops) {
	vmm_driver *driver = (vmm_driver*)ops->find_module_by_type(MODULE_VMM);
vmm_alloc = driver->vmm->alloc;
vmm_free = driver->vmm->free;
vmm_v2p = driver->vmm->v2p;
vmm_handle_fault = driver->vmm->handle_fault;
}

#endif
#endif