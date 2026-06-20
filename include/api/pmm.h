/**
 * @file pmm_api.h
 * @brief Unified Global VTable Interface for the Binary-Buddy Physical Memory Manager.
 *
 * Exposes an object-oriented operational dispatch table managing raw physical RAM page frames.
 * Handles buddy allocation splits, recursive coalescing merges, and hardware zone boundary limits.
 */

#ifndef PMM_API_H
#define PMM_API_H

#include <type.h>

/**
 * @struct pmm_interface
 * @brief System operational function table for physical page frame allocations.
 *
 * Implements a lookaside metadata-backed Binary-Buddy engine tracking up to order-scaled allocation pairs,
 * abstracting hardware topology parsing, DMA address constraints, and page reference counting safely.
 */
typedef struct pmm_interface 
{
    /* =========================================================================
     * AREA 1: Core Frame Allocation Engine
     * ========================================================================= */

    /**
     * @brief Allocates a naturally aligned power-of-two block of physical page frames.
     *
     * Queries lookaside order tracking streams in $O(1)$ constant time. If the targeted order list 
     * is depleted, the engine searches upward to higher orders, unlinking a larger block and recursively 
     * splitting it into matched "buddy" fractions until the requested allocation order is satisfied.
     *
     * @param[in]  page_order Power-of-two size exponent factor determining block size ($2^{\text{order}}$ pages).
     * @param[out] out_frame  Destination pointer capturing the base physical address of the allocated block.
     *
     * @retval 0      Success. Block allocated smoothly; out_frame updated.
     * @retval EINVAL The requested page_order exceeds system limits, or out_frame evaluated to NULL.
     * @retval ENOMEM System memory exhaustion; no available blocks or larger buddies to split.
     */
    int (*alloc_page)(u8 page_order, u64 *out_frame);

    /* =========================================================================
     * AREA 2: Constraint-Driven Allocations
     * ========================================================================= */

    /**
     * @brief Allocates continuous page frames matching a strict physical alignment mask.
     *
     * Sweeps lookaside list nodes looking for an explicit block whose physical address is an even 
     * multiple of the alignment criteria. Useful for populating structural execution tables or configuring 
     * specific device drivers (e.g., disk controller buffers) requiring high-order physical boundaries.
     * Splitting rules are then executed downward on the matched node.
     *
     * @param[in]  count     Total number of sequential physical pages requested.
     * @param[in]  alignment Strict physical boundary constraint constraint in bytes (must be a power-of-two).
     * @param[out] out       Destination tracking pointer capturing the base physical block address.
     *
     * @retval 0      Success. Perfectly aligned physical memory blocks have been isolated and bound.
     * @retval EINVAL Passed validation failures (e.g., alignment is not a power-of-two, count is 0, or out is NULL).
     * @retval ENOMEM Failed to locate any free tracking blocks matching the required alignment boundaries.
     */
    int (*alloc_aligned)(u64 count, u64 alignment, u64 *out);

    /**
     * @brief Allocates continuous pages guaranteed to fall below a maximum physical address threshold.
     *
     * Traverses the tracking orders to secure blocks whose span fits within the low-memory physical architecture 
     * limits (`base + size <= max_addr`). Essential for handling legacy 32-bit hardware peripherals or 
     * ISA devices bound to narrow DMA addressing zones (e.g., lower 16MB or 4GB RAM thresholds).
     *
     * @param[in]  count    Total continuous physical page frame block volume requested.
     * @param[in]  max_addr Maximum acceptable physical address boundary limit (exclusive/inclusive cap).
     * @param[out] out      Destination tracking pointer capturing the qualified low-zone physical address.
     *
     * @retval 0      Success. Qualified memory range isolated and successfully written to out.
     * @retval EINVAL Passed parameters are invalid, or count scales past structural tracking bounds.
     * @retval ENOMEM No available free blocks isolated within the specified physical address zone boundary.
     */
    int (*alloc_in_range)(u64 count, u64 max_addr, u64 *out);

    /* =========================================================================
     * AREA 3: Page Reference Tracking and Lifecycle Management
     * ========================================================================= */

    /**
     * @brief Increments the atomic sharing reference count tracked against a physical page frame.
     *
     * Safely locks structural page records, boosting the target cell's internal reference counter.
     * Crucial for coordinating Shared Memory channels, Copy-On-Write (COW) forks, and virtual IPC mappings 
     * where multiple isolated spaces safely share a single underlying physical RAM address frame.
     *
     * @param[in] frame Ground physical base address mapping the target frame to retain.
     *
     * @retval 0      Success. The page frame reference count has been incremented.
     * @retval EFAULT The physical frame address falls outside the tracking array limits, or is already marked free.
     */
    int (*retain)(u64 frame);

    /**
     * @brief Decrements a page frame's reference counter, automatically recycling it if the count drops to zero.
     *
     * Drops the target frame's reference counter by one. If the counter hits zero, the frame is marked free 
     * and a LIFO buddy-reclaim cascade is triggered. This loop checks adjacent buddy pairs via XOR bit operations, 
     * merging matching blocks back into single higher-order blocks recursively up the lookaside layers.
     *
     * @param[in] frame Ground physical base address mapping the target frame to drop.
     *
     * @retval 0      Success. Reference counter dropped; page successfully coalesced if unreferenced.
     * @retval EFAULT The physical page frame is invalid, or its internal reference counter is already zero.
     */
    int (*release)(u64 frame);

    /* =========================================================================
     * AREA 4: Diagnostics and Reservation Profiles
     * ========================================================================= */

    /**
     * @brief Queries the cumulative capacity metrics tracking total usable physical memory.
     *
     * @return Total capacity size evaluated across all available, non-reserved system RAM sectors in bytes.
     */
    u64 (*get_total_memory)(void);

    /**
     * @brief Queries the active capacity metrics tracking unallocated physical memory.
     *
     * @return Total available space sizing footprint currently idling inside lookaside free lists in bytes.
     */
    u64 (*get_free_memory)(void);

    /**
     * @brief Forcefully evicts a continuous range of pages from free lookaside structures during initialization.
     *
     * Loops through the requested address range, unlinking any intersecting free blocks from the buddy lookaside 
     * lists. It marks these blocks as allocated (`is_free = 0`) and sets their tracking properties to safe default state parameters. 
     * This protects critical architecture zones, kernel code ranges, ACPI tables, or initial ramdisk images from 
     * being overwritten by the allocation engine.
     *
     * @param[in] start Physical starting boundary address locating the target section to reserve (must be page-aligned).
     * @param[in] sz    Total raw dimension footprint scale tracking target memory cells in bytes.
     *
     * @retval 0      Success. Target ranges have been stripped out of free pools and safely reserved.
     * @retval EINVAL The requested address bounds exceed the physical limits tracked by the metadata array.
     */
    int (*reserve_range)(u64 start, u64 sz);

} pmm_interface_t;

#endif /* PMM_API_H */
