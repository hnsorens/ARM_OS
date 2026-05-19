#ifndef VMM_API_H
#define VMM_API_H

#include "../type.h"
#include "mmu.h"

typedef enum vmm_region_type {
    VMM_REGION_FREE    = 0,
    VMM_REGION_CODE    = 1, // Executable code (.text)
    VMM_REGION_DATA    = 2, // Read/Write data (.data, .bss)
    VMM_REGION_STACK   = 3, // Thread stacks (grows down)
    VMM_REGION_HEAP    = 4, // Dynamic memory (grows up)
    VMM_REGION_MMIO    = 5, // Hardware device registers
    VMM_REGION_GUARD   = 6  // Unmappable "no-man's land" to catch overflows
} vmm_region_type_t;

typedef struct vmm_region_info {
    vaddr_t       base;      // Starting virtual address
    size_t            size;      // Size in bytes (multiple of 4KB)
    mmu_flags_t       flags;     // R/W/X permissions
    vmm_region_type_t type;      // What this memory is for
    bool              is_paged;  // Is it backed by RAM or is it "reserved"?
} vmm_region_info_t;

typedef struct vmm_interface 
{
    /* --- Address Space Lifecycle --- */
    
    // Returns the physical address of the new L0/Level 4 page table
    k_status_t (*space_create)(paddr_t *out_table_root);
    k_status_t (*space_destroy)(paddr_t table_root);

    /* --- Core Allocation --- */

    // vaddr is an "in-out" param: if *vaddr is NULL, VMM picks a spot.
    k_status_t (*allocate)(paddr_t root, vaddr_t *vaddr, size_t sz, mmu_flags_t flags, vmm_region_type_t type);
    k_status_t (*reserve)(paddr_t root, vaddr_t vaddr, size_t sz);
    k_status_t (*free)(paddr_t root, vaddr_t vaddr, size_t sz);
    k_status_t (*resize)(paddr_t root, vaddr_t vaddr, size_t old_sz, size_t new_sz);

    /* --- Special Mapping --- */

    k_status_t (*map_external)(paddr_t root, vaddr_t v, paddr_t p, size_t sz, mmu_flags_t f);
    k_status_t (*protect)(paddr_t root, vaddr_t vaddr, size_t sz, mmu_flags_t new_flags);

    /* --- Introspection & Control --- */

    k_status_t (*query)(paddr_t root, vaddr_t vaddr, vmm_region_info_t *out_info);
    
    // Switch the CPU to use this table root (and handles ASID/PCID internally)
    k_status_t (*activate)(paddr_t root);

    // Synchronize caches across CPUs
    k_status_t (*sync)(paddr_t root, vaddr_t vaddr, size_t sz);

} vmm_interface_t;

#endif
