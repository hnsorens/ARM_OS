#ifndef __MMU_INC_H__
#define __MMU_INC_H__


#include "mmu_driver.h"

#ifdef __MAIN__
#define GLOBAL __attribute__((visibility("hidden")))
#define END = 0;
#else
#define GLOBAL __attribute__((visibility("hidden"))) extern
#define END ;
#endif

  /* --- 2. LIFECYCLE MANAGEMENT --- */ 
    // Allocates the root page directory (PGD/PML4) 
GLOBAL int (*mmu_ctx_init)( mmu_context_t* ctx ) END 
    // Tears down the entire tree and frees page-table pages 
GLOBAL void (*mmu_ctx_destroy)( mmu_context_t* ctx ) END 
    // The "Switch": Loads the page table into the CPU (CR3 / TTBR0) 
GLOBAL int (*mmu_activate)( mmu_context_t* ctx ) END 
 
GLOBAL paddr_t (*mmu_v2p_kernel)( vaddr_t va ) END 
GLOBAL paddr_t (*mmu_v2p)( mmu_context_t* ctx, vaddr_t va ) END 
 
    /* --- 3. THE CORE TRANSLATION ENGINE --- */ 
GLOBAL int (*mmu_map_kernel)( vaddr_t va, paddr_t pa, size_t sz, prot_t prot ) END 
GLOBAL int (*mmu_unmap_kernel)( vaddr_t va, size_t size ) END 
    // Map: Handles 'walking' the tree and allocating intermediate levels 
GLOBAL int (*mmu_map)( mmu_context_t* ctx, vaddr_t va, paddr_t pa, size_t sz, prot_t prot ) END 
    // Unmap: Removes entries and triggers internal accounting 
GLOBAL int (*mmu_unmap)( mmu_context_t* ctx, vaddr_t va, size_t sz ) END 
    // Protect: Changes R/W/X or Cache attributes without unmapping 
GLOBAL int (*mmu_protect)( mmu_context_t* ctx, vaddr_t va, size_t sz, prot_t new_prot ) END 
 
    /* --- 4. COHERENCY & BARRIERS --- */ 
    // Local Flush: Invalidates this CPU's TLB for a specific range 
GLOBAL void (*mmu_tlb_flush_local)( mmu_context_t* ctx, vaddr_t va, size_t sz ) END 
    // Global Flush (Shootdown): Forces ALL CPUs to drop these TLB entries 
GLOBAL void (*mmu_tlb_flush_all)( mmu_context_t* ctx ) END 
    // Instruction Sync: Required for JITs (flushes I-Cache) 
GLOBAL void (*mmu_sync_instr)( mmu_context_t* ctx, vaddr_t va, size_t sz ) END 
 
    /* --- 5. FAULT & INTROSPECTION --- */ 
    // Translates VA to PA manually (useful for DMA/Drivers) 
GLOBAL paddr_t (*mmu_virt_to_phys)( mmu_context_t* ctx, vaddr_t va ) END 
    // Called by the Page Fault Handler to fix/demand-page a miss 
GLOBAL int (*mmu_handle_fault)( mmu_context_t* ctx, vaddr_t va, flags_t flags ) END 
 
    /* --- 6. ADVANCED TELEMETRY --- */ 
    // Dirty Tracking: Has this page been written to? (Critical for VM Migration) 
GLOBAL int (*mmu_is_dirty)( mmu_context_t* ctx, vaddr_t va ) END 
GLOBAL void (*mmu_clear_dirty)( mmu_context_t* ctx, vaddr_t va ) END 
    // Accessed Tracking: Has this page been touched? (For LRU Swap logic) 
GLOBAL int (*mmu_is_accessed)( mmu_context_t* ctx, vaddr_t va ) END 

#ifdef __MAIN__

static void mmu_fetch(core_ops *ops) {
	mmu_driver *driver = (mmu_driver*)ops->find_module_by_type(MODULE_MMU);
mmu_ctx_init = driver->mmu->ctx_init;
mmu_ctx_destroy = driver->mmu->ctx_destroy;
mmu_activate = driver->mmu->activate;
mmu_v2p_kernel = driver->mmu->v2p_kernel;
mmu_v2p = driver->mmu->v2p;
mmu_map_kernel = driver->mmu->map_kernel;
mmu_unmap_kernel = driver->mmu->unmap_kernel;
mmu_map = driver->mmu->map;
mmu_unmap = driver->mmu->unmap;
mmu_protect = driver->mmu->protect;
mmu_tlb_flush_local = driver->mmu->tlb_flush_local;
mmu_tlb_flush_all = driver->mmu->tlb_flush_all;
mmu_sync_instr = driver->mmu->sync_instr;
mmu_virt_to_phys = driver->mmu->virt_to_phys;
mmu_handle_fault = driver->mmu->handle_fault;
mmu_is_dirty = driver->mmu->is_dirty;
mmu_clear_dirty = driver->mmu->clear_dirty;
mmu_is_accessed = driver->mmu->is_accessed;
}

#endif
#endif