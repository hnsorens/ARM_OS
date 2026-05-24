#ifndef PMM_API_H
#define PMM_API_H

#include "../type.h"

typedef struct pmm_interface 
{
    /* --- Core Allocation --- */
    int (*alloc_page)(u8 page_order, u64 *out_frame);
    int (*free_page)(u8 page_order, u64 frame);

    /* --- The Mandatory Additions --- */

    // 1. Allocate with Alignment/Boundary constraints
    // Necessary for hardware devices that require memory starting at a specific 
    // boundary (e.g., a 64KB-aligned buffer for a disk controller).
    int (*alloc_aligned)(u64 count, u64 alignment, u64 *out);

    // 2. Zone-Specific Allocation (DMA/DMA32/Normal)
    // On x86/ARM, some hardware can only "see" the first 16MB or 4GB of RAM.
    // This tells the PMM: "Give me memory from the low-address zone."
    int (*alloc_in_range)(u64 count, u64 max_addr, u64 *out);

    // 3. Page Reference Counting
    // Critical for "Shared Memory." If two processes use the same physical page,
    // the PMM shouldn't actually free it until BOTH processes are done with it.
    void (*retain)(u64 frame);
    void (*release)(u64 frame); // If count hits 0, it calls free_page()

    /* --- Statistics --- */
    u64 (*get_total_memory)(void);
    u64 (*get_free_memory)(void);
    int (*reserve_range)(u64 start, u64 sz);

} pmm_interface_t;

#endif
