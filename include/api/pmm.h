#ifndef PMM_API_H
#define PMM_API_H

#include "../type.h"

typedef struct pmm_interface 
{
    /* --- Core Allocation --- */
    k_status_t (*alloc_page)(uint8_t page_order, phys_addr_t *out_frame);
    k_status_t (*alloc_pages)(uint8_t page_order, size_t count, phys_addr_t *out_frames);
    k_status_t (*free_page)(uint8_t page_order, phys_addr_t frame);
    k_status_t (*free_pages)(uint8_t page_order, phys_addr_t frames, size_t count);

    /* --- The Mandatory Additions --- */

    // 1. Allocate with Alignment/Boundary constraints
    // Necessary for hardware devices that require memory starting at a specific 
    // boundary (e.g., a 64KB-aligned buffer for a disk controller).
    k_status_t (*alloc_aligned)(size_t count, size_t alignment, phys_addr_t *out);

    // 2. Zone-Specific Allocation (DMA/DMA32/Normal)
    // On x86/ARM, some hardware can only "see" the first 16MB or 4GB of RAM.
    // This tells the PMM: "Give me memory from the low-address zone."
    k_status_t (*alloc_in_range)(size_t count, phys_addr_t max_addr, phys_addr_t *out);

    // 3. Page Reference Counting
    // Critical for "Shared Memory." If two processes use the same physical page,
    // the PMM shouldn't actually free it until BOTH processes are done with it.
    void (*retain)(phys_addr_t frame);
    void (*release)(phys_addr_t frame); // If count hits 0, it calls free_page()

    /* --- Statistics --- */
    size_t (*get_total_memory)(void);
    size_t (*get_free_memory)(void);
    k_status_t (*reserve_range)(phys_addr_t start, size_t sz);

} pmm_interface_t;

#endif
