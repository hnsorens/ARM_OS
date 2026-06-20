#ifndef VMM_API_H
#define VMM_API_H

#include <type.h>
#include "mmu.h"

/**
 * @enum vmm_region_type
 * @brief Categorization definitions tracking the operational intent of a Virtual Memory Area.
 */
enum vmm_region_type {
    VMM_REGION_FREE    = 0, /**< Unassigned, clear virtual layout address block */
    VMM_REGION_CODE    = 1, /**< Executable code segments (.text) */
    VMM_REGION_DATA    = 2, /**< Read/Write initialized or zeroed data segments (.data, .bss) */
    VMM_REGION_STACK   = 3, /**< Thread execution storage zones (traditionally descending downward) */
    VMM_REGION_HEAP    = 4, /**< Dynamic, variable allocator storage ranges (traditionally ascending upward) */
    VMM_REGION_MMIO    = 5, /**< Memory-mapped peripheral device hardware registers */
    VMM_REGION_GUARD   = 6  /**< Deliberately unmappable "no-man's land" ranges engineered to intercept overflows */
};

/**
 * @struct vmm_region_info
 * @brief Public analytical capture profile representing a query pass over an active VMA.
 */
struct vmm_region_info {
    u64                  base;     /**< Ground virtual address boundary coordinate */
    u64                  size;     /**< Footprint volume scale calculated in bytes (always page-aligned) */
    enum mmu_flags       flags;    /**< Active architectural execution and read/write hardware bit permissions */
    enum vmm_region_type type;     /**< Functional classification role matching the address space area */
    bool                 is_paged; /**< True if explicitly mapped to real physical frames via PMM lookups */
};

/**
 * @struct vmm_interface
 * @brief Object-oriented system operational dispatch layout managing virtual memory allocation spaces.
 *
 * Implements an intrusive lookaside-allocated Binary Search Tree tracker mapping virtual regions,
 * abstracting complex tasks like protection bit mutations, split branches, and address range allocation hunts.
 */
typedef struct vmm_interface 
{
    /* =========================================================================
     * AREA 1: Address Space Lifecycle
     * ========================================================================= */

    /**
     * @brief Instantiates an isolated virtual address workspace context.
     *
     * Requests a clean L0/Level 4 core translation base directory via the MMU interface, 
     * registers an active workspace descriptor block within the global registry streams,
     * and primes the tracker's internal structural search tree nodes.
     *
     * @param[out] out_table_root Physical address pointer destination capturing the root frame of the directory.
     *
     * @retval 0      Success. The tracking workspace is mapped and ready for utilization.
     * @retval EINVAL The destination handle out_table_root pointer evaluates to NULL.
     * @retval ENOMEM Direct physical page allocation routines fail during MMU table provisioning.
     */
    int (*space_create)(u64 *out_table_root);

    /**
     * @brief Dismantles an active address space context and yields all resources back to core subsystems.
     *
     * Iterates down the internal structural binary search tree, dropping metadata tracking components.
     * For all allocations marked `is_paged == true`, it unmaps active ranges, releases underlying physical 
     * page frames to the PMM, drops the parent translation tables, and scrubs the descriptor from global registries.
     *
     * @warning Precondition: The targeted space MUST NOT be actively loaded into a CPU core's CR3 execution state.
     *
     * @param[in] table_root Physical address root locating the targeted level 4 table directory to dismantle.
     *
     * @retval 0      Success. Virtual context space and physical tables have been eviscerated.
     * @retval EINVAL The requested context table_root handle was not found within active global registries.
     */
    int (*space_destroy)(u64 table_root);

    /* =========================================================================
     * AREA 2: Memory Allocations and Mapping Modifiers
     * ========================================================================= */

    /**
     * @brief Reserves an unallocated virtual address window, backing it with physical memory frames.
     *
     * Scans for continuous address space holes using $O(\log N)$ tree lookups (falling back to a linear search 
     * if address collision verification probes fail). Once verified, it captures frame allocations from the 
     * PMM, links the virtual coordinates straight to the physical nodes inside MMU structures, and builds a VMA node.
     *
     * @param[in]     root   Physical address root tracking the context translation map directory.
     * @param[in,out] vaddr  Target placement hint address. Overwritten with the final assigned base address.
     * @param[in]     sz     Total byte capacity tracking structural size requirements (aligned to 4KB blocks).
     * @param[in]     flags  Hardware execution protection settings (Read, Write, User-space, No-Execute).
     * @param[in]     type   The operational destination categorization tag of the generated memory pool.
     *
     * @retval 0      Success. Virtual mapping constraints are successfully bound and output to vaddr.
     * @retval EINVAL Target root or vaddr pointer evaluates to NULL, or sz equals zero.
     * @retval ENOMEM Insufficient system memory to fulfill physical frames or metadata descriptors.
     */
    int (*allocate)(u64 root, u64 *vaddr, u64 sz, enum mmu_flags flags, enum vmm_region_type type);

    /**
     * @brief Carves out a physical hardware memory-mapped IO device register address profile.
     *
     * bypasses physical frame generation steps via the PMM. Constructs an isolated VMA tracking node tagged 
     * as `VMM_REGION_MMIO`, forcing the MMU configuration tables to bind the requested virtual coordinates 
     * straight to explicit external hardware device address lines.
     *
     * @param[in] root Physical address root tracking the context translation map directory.
     * @param[in] v    Desired target virtual base address coordinate block mapping the hardware layout.
     * @param[in] p    Raw base physical address locating the foreign device peripheral hardware registers.
     * @param[in] sz   Total space volume scale tracking hardware dimensions in bytes (aligned to 4KB).
     * @param[in] f    Hardware execution protection flags (traditionally Cache-Disable/Write-Through profiles).
     *
     * @retval 0      Success. Hardware MMIO register maps are anchored into virtual contexts.
     * @retval EINVAL The requested target workspace handle is invalid.
     * @retval EEXIST Target range choices clash with an existing virtual mapping registration area.
     * @retval ENOMEM Structural metadata tracking slab elements undergo resource depletion.
     */
    int (*map_external)(u64 root, u64 v, u64 p, u64 sz, enum mmu_flags f);

    /**
     * @brief Establishes an unmappable memory protection buffer zone.
     *
     * Reserves a virtual address range inside the tracking tree without committing physical pages or MMU table 
     * maps (`is_paged = false`). Acts as a "no-man's land" trap. Any execution thread attempting to read or 
     * write within these boundaries instantly fires an unrecoverable page fault exception.
     *
     * @param[in] root  Physical address root tracking the context translation map directory.
     * @param[in] vaddr Virtual target base address locating the guard region.
     * @param[in] sz    Total protection block layout boundary limits scale evaluated in bytes.
     *
     * @retval 0      Success. Protection layout zone registered in search configurations.
     * @retval EINVAL Target parameters fail verification tests, or sz equals zero.
     * @retval EEXIST The target coordinate region intersects with a pre-existing memory layout allocation.
     * @retval ENOMEM Lookaside tracking allocator elements undergo resource exhaustion.
     */
    int (*reserve)(u64 root, u64 vaddr, u64 sz);

    /* =========================================================================
     * AREA 3: Mapping Alterations and Eviction Routines
     * ========================================================================= */

    /**
     * @brief Unmaps virtual address regions, wiping memory cells and reclaiming physical structures.
     *
     * Clears page records inside MMU architecture directories, releases active physical memory blocks to the PMM,
     * and alters the search tree mapping trackers. Depending on the virtual address range passed, this method automatically 
     * processes full node removals, edge trims, or splits an existing VMA into two distinct left and right child nodes.
     *
     * @param[in] root  Physical address root tracking the context translation map directory.
     * @param[in] vaddr Targeted virtual ground coordinate locating the section to unmap.
     * @param[in] sz    Total clearance width dimension scale tracking target cells in bytes.
     *
     * @retval 0      Success. Range unmapped, hardware tables cleared, physical structures recycled.
     * @retval EINVAL Target range choices fail alignment limits or fall outside registered VMA boundaries.
     * @retval ENOMEM Split-slice operations fail due to a lack of lookaside structural tracking blocks.
     */
    int (*free)(u64 root, u64 vaddr, u64 sz);

    /**
     * @brief Resizes an active virtual allocation block, scaling boundaries dynamically.
     *
     * If contracting (`new_sz < old_sz`), it triggers trailing page eviction sweeps via internal `free` loops.
     * If expanding (`new_sz > old_sz`), it verifies contiguous layout availability adjacent to the current boundary.
     * On verification success, it allocates additional pages via the PMM and maps them forward into place.
     *
     * @param[in] root    Physical address root tracking the context translation map directory.
     * @param[in] vaddr   Virtual target base coordinate locating the target structural area to mutate.
     * @param[in] old_sz  The exact pre-existing allocation capacity footprint parameter in bytes.
     * @param[in] new_sz  Desired scale dimensions updating target configurations in bytes.
     *
     * @retval 0      Success. Allocations scaled smoothly without displacing original elements.
     * @retval EINVAL Validation checks fail (e.g., base or old size metrics do not match tree parameters).
     * @retval ENOMEM Expansion fails because neighboring addresses are blocked by an existing VMA or memory limits.
     */
    int (*resize)(u64 root, u64 vaddr, u64 old_sz, u64 new_sz);

    /**
     * @brief Mutates architectural permission access bits across a targeted memory area.
     *
     * Instructs the MMU to rewrite the translation tables for the specified virtual address range.
     * If the target span only updates a fragment of an existing VMA block, this function manages 
     * structural tree transformations, breaking the target into up to three distinct chunks (left-unaltered, 
     * middle-mutated, right-unaltered) using lookaside element configurations.
     *
     * @param[in] root      Physical address root tracking the context translation map directory.
     * @param[in] vaddr     Starting virtual coordinate mapping the boundary layout modifications.
     * @param[in] sz        Total target length dimension tracking cell updates in bytes.
     * @param[in] new_flags Updated security access parameter bits to assign to the targeted memory cells.
     *
     * @retval 0      Success. Privileges committed safely across all mapped layers.
     * @retval EINVAL Target range parameters do not map cleanly inside a registered tracking structure block.
     * @retval ENOMEM Subsystem undergoes allocation failure while splitting tracking tree metadata elements.
     */
    int (*protect)(u64 root, u64 vaddr, u64 sz, enum mmu_flags new_flags);

    /* =========================================================================
     * AREA 4: Introspection and Hardware Synchronizations
     * ========================================================================= */

    /**
     * @brief Audits an active workspace context, reading properties bound to an address coordinate.
     *
     * Leverages a fast single-element cache checkpoint for temporal speedups before executing $O(\log N)$ 
     * binary search tree evaluations. On success, snapshots the region properties into a user-facing info structure.
     *
     * @param[in]  root     Physical address root tracking the context translation map directory.
     * @param[in]  vaddr    Virtual target coordinate being queried.
     * @param[out] out_info Target data collection capture profile structure payload reference.
     *
     * @retval 0      Success. Matching region verified and profile copied to out_info.
     * @retval EINVAL The requested workspace structure was not found, or the address is completely unmapped.
     */
    int (*query)(u64 root, u64 vaddr, struct vmm_region_info *out_info);

    /**
     * @brief Forces active virtual memory directory trees into actual hardware execution contexts.
     *
     * Hands off configuration data directly to system processing architectures, overriding active memory spaces
     * by refreshing control registers (e.g., streaming the table root directly into the CPU's CR3 register).
     *
     * @param[in] root Physical address root tracking the context translation map directory to load.
     *
     * @retval 0      Success. CPU execution architectures are actively focused on the target context page map.
     * @retval EINVAL Error occurred during context swapping processes inside hardware operations.
     */
    int (*activate)(u64 root);

    /**
     * @brief Flushes Translation Lookaside Buffers (TLB) to align hardware with internal page changes.
     *
     * Computes the total required page steps and forces individual address line cache invalidations, 
     * ensuring old or altered routing translations are cleared from core processing hardware elements.
     *
     * @param[in] root  Physical address root tracking the context translation map directory.
     * @param[in] vaddr Target base virtual address requiring hardware synchronization sweeps.
     * @param[in] sz    Total synchronization scale width calculated in bytes.
     *
     * @retval 0      Success. TLB cache lines successfully evicted.
     */
    int (*sync)(u64 root, u64 vaddr, u64 sz);

} vmm_interface_t;

#endif /* VMM_API_H */
