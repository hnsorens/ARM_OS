#ifndef VMM_VTABLE_H
#define VMM_VTABLE_H

#include "vmm_types.h"

/**
 * @brief Virtual Memory Manager
 * 
 */
typedef struct vmm_ops {
  
    vaddr_t (*alloc)(vmm_context *ctx, size_t sz, flags_t flags);
    vaddr_t (*alloc_kernel)(size_t sz, flags_t flags);
    
    // The "Cleanup": Unmap from MMU, tell PMM to free the frames.
    int (*free)(vmm_context *ctx, vaddr_t v, size_t sz);
    int (*free_kernel)(vaddr_t v, size_t sz);

    // The "Translator": Virtual to Physical (usually a walk of the MMU tables).
    paddr_t (*v2p)(vmm_context *ctx, vaddr_t va);
    paddr_t (*v2p_kernel)(vaddr_t va);

    // The "Emergency": What to do when a CPU hits an unmapped address.
    int (*handle_fault)(vmm_context *ctx, vaddr_t addr, err_t err);
} vmm_ops;

#endif