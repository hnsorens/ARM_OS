#ifndef VMM_API_H
#define VMM_API_H

#include "../type.h"
#include "mmu.h"

enum vmm_region_type {
    VMM_REGION_FREE    = 0,
    VMM_REGION_CODE    = 1, // Executable code (.text)
    VMM_REGION_DATA    = 2, // Read/Write data (.data, .bss)
    VMM_REGION_STACK   = 3, // Thread stacks (grows down)
    VMM_REGION_HEAP    = 4, // Dynamic memory (grows up)
    VMM_REGION_MMIO    = 5, // Hardware device registers
    VMM_REGION_GUARD   = 6  // Unmappable "no-man's land" to catch overflows
};

struct vmm_region_info {
    u64       base;      // Starting virtual address
    u64            size;      // Size in bytes (multiple of 4KB)
    enum mmu_flags       flags;     // R/W/X permissions
    enum vmm_region_type type;      // What this memory is for
    bool              is_paged;  // Is it backed by RAM or is it "reserved"?
};

typedef struct vmm_interface 
{
    /* --- Address Space Lifecycle --- */
    
    // Returns the physical address of the new L0/Level 4 page table
    int (*space_create)(u64 *out_table_root);
    int (*space_destroy)(u64 table_root);

    /* --- Core Allocation --- */

    // vaddr is an "in-out" param: if *vaddr is NULL, VMM picks a spot.
    int (*allocate)(u64 root, u64 *vaddr, u64 sz, enum mmu_flags flags, enum vmm_region_type type);
    int (*reserve)(u64 root, u64 vaddr, u64 sz);
    int (*free)(u64 root, u64 vaddr, u64 sz);
    int (*resize)(u64 root, u64 vaddr, u64 old_sz, u64 new_sz);

    /* --- Special Mapping --- */

    int (*map_external)(u64 root, u64 v, u64 p, u64 sz, enum mmu_flags f);
    int (*protect)(u64 root, u64 vaddr, u64 sz, enum mmu_flags new_flags);

    /* --- Introspection & Control --- */

    int (*query)(u64 root, u64 vaddr, struct vmm_region_info *out_info);
    
    // Switch the CPU to use this table root (and handles ASID/PCID internally)
    int (*activate)(u64 root);

    // Synchronize caches across CPUs
    int (*sync)(u64 root, u64 vaddr, u64 sz);

} vmm_interface_t;

#endif
