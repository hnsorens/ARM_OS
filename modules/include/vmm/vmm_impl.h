#ifndef __VMM_INC_H__
#define __VMM_INC_H__


#include "vmm_types.h"

#ifndef CONCAT_HIDDEN
#define CONCAT_HIDDEN(a, b) a ## b
#define CONCAT(a, b) CONCAT_HIDDEN(a, b)
#endif

#define vmm_alloc CONCAT(IMPL_NAME, _alloc_func)
#define vmm_alloc_kernel CONCAT(IMPL_NAME, _alloc_kernel_func)
#define vmm_free CONCAT(IMPL_NAME, _free_func)
#define vmm_free_kernel CONCAT(IMPL_NAME, _free_kernel_func)
#define vmm_v2p CONCAT(IMPL_NAME, _v2p_func)
#define vmm_v2p_kernel CONCAT(IMPL_NAME, _v2p_kernel_func)
#define vmm_handle_fault CONCAT(IMPL_NAME, _handle_fault_func)

__attribute__((used)) vaddr_t vmm_alloc( vmm_context* ctx, size_t sz, flags_t flags );
__attribute__((used)) vaddr_t vmm_alloc_kernel( size_t sz, flags_t flags );
 
    // The "Cleanup": Unmap from MMU, tell PMM to free the frames. 
__attribute__((used)) int vmm_free( vmm_context* ctx, vaddr_t v, size_t sz );
__attribute__((used)) int vmm_free_kernel( vaddr_t v, size_t sz );
 
    // The "Translator": Virtual to Physical (usually a walk of the MMU tables). 
__attribute__((used)) paddr_t vmm_v2p( vmm_context* ctx, vaddr_t va );
__attribute__((used)) paddr_t vmm_v2p_kernel( vaddr_t va );
 
    // The "Emergency": What to do when a CPU hits an unmapped address. 
__attribute__((used)) int vmm_handle_fault( vmm_context* ctx, vaddr_t addr, err_t err );
typedef void (*init_fn_t)(void);
#define __init_func __attribute__((section(".init_array"), used))
#define MODULE_INIT(func) \
static init_fn_t __init_ptr##func __init_func = func;
#endif