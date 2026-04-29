#ifndef __MMU_INC_H__
#define __MMU_INC_H__


#include "mmu_types.h"

#ifndef CONCAT_HIDDEN
#define CONCAT_HIDDEN(a, b) a ## b
#define CONCAT(a, b) CONCAT_HIDDEN(a, b)
#endif

#define mmu_ctx_init CONCAT(IMPL_NAME, _ctx_init_func)
#define mmu_ctx_destroy CONCAT(IMPL_NAME, _ctx_destroy_func)
#define mmu_activate CONCAT(IMPL_NAME, _activate_func)
#define mmu_v2p_kernel CONCAT(IMPL_NAME, _v2p_kernel_func)
#define mmu_v2p CONCAT(IMPL_NAME, _v2p_func)
#define mmu_map_kernel CONCAT(IMPL_NAME, _map_kernel_func)
#define mmu_unmap_kernel CONCAT(IMPL_NAME, _unmap_kernel_func)
#define mmu_map CONCAT(IMPL_NAME, _map_func)
#define mmu_unmap CONCAT(IMPL_NAME, _unmap_func)
#define mmu_protect CONCAT(IMPL_NAME, _protect_func)
#define mmu_tlb_flush_local CONCAT(IMPL_NAME, _tlb_flush_local_func)
#define mmu_tlb_flush_all CONCAT(IMPL_NAME, _tlb_flush_all_func)
#define mmu_sync_instr CONCAT(IMPL_NAME, _sync_instr_func)
#define mmu_virt_to_phys CONCAT(IMPL_NAME, _virt_to_phys_func)
#define mmu_handle_fault CONCAT(IMPL_NAME, _handle_fault_func)
#define mmu_is_dirty CONCAT(IMPL_NAME, _is_dirty_func)
#define mmu_clear_dirty CONCAT(IMPL_NAME, _clear_dirty_func)
#define mmu_is_accessed CONCAT(IMPL_NAME, _is_accessed_func)

  /* --- 2. LIFECYCLE MANAGEMENT --- */ 
    // Allocates the root page directory (PGD/PML4) 
__attribute__((used)) int mmu_ctx_init( mmu_context_t* ctx );
    // Tears down the entire tree and frees page-table pages 
__attribute__((used)) void mmu_ctx_destroy( mmu_context_t* ctx );
    // The "Switch": Loads the page table into the CPU (CR3 / TTBR0) 
__attribute__((used)) int mmu_activate( mmu_context_t* ctx );
 
__attribute__((used)) paddr_t mmu_v2p_kernel( vaddr_t va );
__attribute__((used)) paddr_t mmu_v2p( mmu_context_t* ctx, vaddr_t va );
 
    /* --- 3. THE CORE TRANSLATION ENGINE --- */ 
__attribute__((used)) int mmu_map_kernel( vaddr_t va, paddr_t pa, size_t sz, prot_t prot );
__attribute__((used)) int mmu_unmap_kernel( vaddr_t va, size_t size );
    // Map: Handles 'walking' the tree and allocating intermediate levels 
__attribute__((used)) int mmu_map( mmu_context_t* ctx, vaddr_t va, paddr_t pa, size_t sz, prot_t prot );
    // Unmap: Removes entries and triggers internal accounting 
__attribute__((used)) int mmu_unmap( mmu_context_t* ctx, vaddr_t va, size_t sz );
    // Protect: Changes R/W/X or Cache attributes without unmapping 
__attribute__((used)) int mmu_protect( mmu_context_t* ctx, vaddr_t va, size_t sz, prot_t new_prot );
 
    /* --- 4. COHERENCY & BARRIERS --- */ 
    // Local Flush: Invalidates this CPU's TLB for a specific range 
__attribute__((used)) void mmu_tlb_flush_local( mmu_context_t* ctx, vaddr_t va, size_t sz );
    // Global Flush (Shootdown): Forces ALL CPUs to drop these TLB entries 
__attribute__((used)) void mmu_tlb_flush_all( mmu_context_t* ctx );
    // Instruction Sync: Required for JITs (flushes I-Cache) 
__attribute__((used)) void mmu_sync_instr( mmu_context_t* ctx, vaddr_t va, size_t sz );
 
    /* --- 5. FAULT & INTROSPECTION --- */ 
    // Translates VA to PA manually (useful for DMA/Drivers) 
__attribute__((used)) paddr_t mmu_virt_to_phys( mmu_context_t* ctx, vaddr_t va );
    // Called by the Page Fault Handler to fix/demand-page a miss 
__attribute__((used)) int mmu_handle_fault( mmu_context_t* ctx, vaddr_t va, flags_t flags );
 
    /* --- 6. ADVANCED TELEMETRY --- */ 
    // Dirty Tracking: Has this page been written to? (Critical for VM Migration) 
__attribute__((used)) int mmu_is_dirty( mmu_context_t* ctx, vaddr_t va );
__attribute__((used)) void mmu_clear_dirty( mmu_context_t* ctx, vaddr_t va );
    // Accessed Tracking: Has this page been touched? (For LRU Swap logic) 
__attribute__((used)) int mmu_is_accessed( mmu_context_t* ctx, vaddr_t va );
typedef void (*init_fn_t)(void);
#define __init_func __attribute__((section(".init_array"), used))
#define MODULE_INIT(func) \
static init_fn_t __init_ptr##func __init_func = func;
#endif