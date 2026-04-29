#ifndef MMU_VTABLE_H
#define MMU_VTABLE_H

#include "mmu_types.h"

/**
 * @brief Memory Management Unit
 * 
 * Manages virtual to physical memory mappings, page tables, and
 * memory protection. Handles kernel space memory mapping operations.
 */
typedef struct mmu_ops
{
  /* --- 2. LIFECYCLE MANAGEMENT --- */
    // Allocates the root page directory (PGD/PML4)
    int (*ctx_init)(mmu_context_t *ctx);
    // Tears down the entire tree and frees page-table pages
    void (*ctx_destroy)(mmu_context_t *ctx);
    // The "Switch": Loads the page table into the CPU (CR3 / TTBR0)
    int  (*activate)(mmu_context_t *ctx);

    paddr_t (*v2p_kernel)(vaddr_t va);
    paddr_t (*v2p)(mmu_context_t *ctx, vaddr_t va);

    /* --- 3. THE CORE TRANSLATION ENGINE --- */
    int  (*map_kernel)(vaddr_t va, paddr_t pa, size_t sz, prot_t prot);
    int  (*unmap_kernel)(vaddr_t va, size_t size);
    // Map: Handles 'walking' the tree and allocating intermediate levels
    int  (*map)(mmu_context_t *ctx, vaddr_t va, paddr_t pa, size_t sz, prot_t prot);
    // Unmap: Removes entries and triggers internal accounting
    int  (*unmap)(mmu_context_t *ctx, vaddr_t va, size_t sz);
    // Protect: Changes R/W/X or Cache attributes without unmapping
    int  (*protect)(mmu_context_t *ctx, vaddr_t va, size_t sz, prot_t new_prot);

    /* --- 4. COHERENCY & BARRIERS --- */
    // Local Flush: Invalidates this CPU's TLB for a specific range
    void (*tlb_flush_local)(mmu_context_t *ctx, vaddr_t va, size_t sz);
    // Global Flush (Shootdown): Forces ALL CPUs to drop these TLB entries
    void (*tlb_flush_all)(mmu_context_t *ctx);
    // Instruction Sync: Required for JITs (flushes I-Cache)
    void (*sync_instr)(mmu_context_t *ctx, vaddr_t va, size_t sz);

    /* --- 5. FAULT & INTROSPECTION --- */
    // Translates VA to PA manually (useful for DMA/Drivers)
    paddr_t (*virt_to_phys)(mmu_context_t *ctx, vaddr_t va);
    // Called by the Page Fault Handler to fix/demand-page a miss
    int  (*handle_fault)(mmu_context_t *ctx, vaddr_t va, flags_t flags);

    /* --- 6. ADVANCED TELEMETRY --- */
    // Dirty Tracking: Has this page been written to? (Critical for VM Migration)
    int (*is_dirty)(mmu_context_t *ctx, vaddr_t va);
    void (*clear_dirty)(mmu_context_t *ctx, vaddr_t va);
    // Accessed Tracking: Has this page been touched? (For LRU Swap logic)
    int (*is_accessed)(mmu_context_t *ctx, vaddr_t va);

} mmu_ops;

#endif