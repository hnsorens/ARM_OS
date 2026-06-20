/**
 * @file mmu_api.h
 * @brief Unified Global VTable Interface for the AArch64 Multi-Level MMU Engine.
 *
 * Exposes an object-oriented hardware abstraction layer managing four-level (L0-L3)
 * virtual address spaces, ASID process contexts, TLB invalidation, and page translation attributes.
 */

#ifndef MMU_API_H
#define MMU_API_H

#include <type.h>

/**
 * @enum mmu_flags
 * @brief Bitmask definitions matching architectural AArch64 Stage 1 Descriptor fields.
 */
enum mmu_flags
{
    /** @brief Read-Only bit access constraint (AP[2]). Mapping bit 0 = Read/Write, 1 = Read-Only. */
    MMU_RO            = (1ULL << 7),  
    
    /** @brief Unprivileged vs Privileged permissions context selector (AP[1]). 1 = EL0 (User), 0 = EL1 (Kernel). */
    MMU_USER          = (1ULL << 6),  

    /** @brief Unprivileged Execute-Never execution barrier (UXN). Inhibits instruction fetches from EL0. */
    MMU_NO_EXEC       = (1ULL << 54), 

    /** @brief Memory Attribute Indirection Register slot offset selector. Maps to Device/Non-Cacheable profiles. */
    MMU_NOCACHE       = (1ULL << 2),  
    
    /** @brief Memory Attribute Indirection Register slot offset selector. Maps to Normal, Write-Through profiles. */
    MMU_WRITE_THROUGH = (2ULL << 2),  
};

/**
 * @enum page_size
 * @brief Hardware-supported virtual translation block allocations and page boundaries.
 */
enum page_size
{
    PS_4KB = 0x1000,      /**< Standard terminal Level 3 page frame granule.             */
    PS_2MB = 0x200000,    /**< Midsize Level 2 block allocation descriptor boundaries.  */
    PS_1GB = 0x40000000,  /**< Monolithic Level 1 block allocation descriptor boundaries.*/
};

/**
 * @struct mmu_interface
 * @brief System operational function table for managing MMU translation matrices.
 *
 * Coordinates deep structural multi-level page tables via PMM bindings, tracking transitions
 * across virtual addressing spaces, memory protection flags, context switches, and TLB consistency barriers.
 */
typedef struct mmu_interface
{
    /* =========================================================================
     * AREA 1: Translation Tree Context Lifecycle Management
     * ========================================================================= */

    /**
     * @brief Allocates and initializes a brand new Level 0 (Root) translation directory page.
     *
     * Requests a zero-order block via physical memory tracking engines, ensuring it is 
     * scrubbed cleanly (`memset` to 0) before export to clear unmapped transient garbage indices.
     *
     * @param[out] out_root Destination pointer to capture the allocated physical address of the tree root.
     *
     * @retval 0      Success; out_root holds the new translation table root.
     * @retval EINVAL The destination tracking pointer provided evaluated to NULL.
     * @retval ENOMEM System memory exhaustion during physical backing collection.
     */
    int (*alloc)(u64 *out_root);

    /**
     * @brief Recursively tears down and frees an entire multi-level translation table tree structure.
     *
     * Walks systematically down active branches (L0 -> L1 -> L2 -> L3), calling reference releases 
     * on transient mid-tier directory frames. It does not release underlying payload data frames.
     *
     * @param[in] root Base physical address anchoring the Level 0 target root matrix to collapse.
     *
     * @retval 0      Success. Tree collapsed and structures unlinked smoothly.
     * @retval EINVAL Passed table root address is zero or invalid.
     */
    int (*free)(u64 root);

    /**
     * @brief Creates an exact physical duplicate clone copy of an active translation context tree.
     *
     * Performs a deep directory copy down the entire tree. Allocates fresh intermediate tables, 
     * mirrors raw terminal entry values, and copies block metrics.
     *
     * @param[in]  src_root  Physical address locating the source template table root to mirror.
     * @param[out] dest_root Destination tracker to capture the newly generated clone tree physical address.
     *
     * @retval 0      Success. Deep cloning path finished without dropping connections.
     * @retval EINVAL Structural validation parameters evaluated to invalid states.
     * @retval ENOMEM Internal directory allocation routines failed mid-flight during deep traversal.
     */
    int (*copy)(u64 src_root, u64 *dest_root);

    /* =========================================================================
     * AREA 2: Core Hardware Context Registers Manipulation
     * ========================================================================= */

    /**
     * @brief Binds a translation tree context into lower half virtual addressing registers (EL0/User Space).
     *
     * Combines the physical root table address with an explicit Address Space Identifier (ASID) tag mask 
     * before modifying hardware control fields (`ttbr0_el1`). Automatically triggers an Instruction 
     * Synchronization Barrier (`isb`) to lock pipeline serialization updates.
     *
     * @param[in] root Physical target base address directing lower half mappings.
     * @param[in] asid Explicit process grouping key context value (ASID tag, bits [63:48]).
     *
     * @retval 0 Always returns success following register deployment.
     */
    int (*set_user_ctx)(u64 root, u16 asid);

    /**
     * @brief Binds a translation tree context into upper half virtual addressing registers (EL1/Kernel Space).
     *
     * Programs the `ttbr1_el1` configuration space, embedding ASID identifiers into kernel operational profiles. 
     * Protects kernel memory mapping models across global context modifications.
     *
     * @param[in] root Physical target base address directing upper half high-memory structures.
     * @param[in] asid Explicit process grouping key context value (ASID tag, bits [63:48]).
     *
     * @retval 0 Always returns success following register deployment.
     */
    int (*set_kernel_ctx)(u64 root, u16 asid);

    /**
     * @brief Extracts the physical base table pointer currently mapped to active User space structures.
     *
     * @param[out] root Target pointer capturing the active physical address extracted from `ttbr0_el1`.
     * @retval 0 Success.
     */
    int (*get_user_ctx)(u64 *root);

    /**
     * @brief Extracts the physical base table pointer currently mapped to active Kernel space structures.
     *
     * @param[out] root Target pointer capturing the active physical address extracted from `ttbr1_el1`.
     * @retval 0 Success.
     */
    int (*get_kernel_ctx)(u64 *root);

    /* =========================================================================
     * AREA 3: Mapping, Unmapping, and Permissions Engineering
     * ========================================================================= */

    /**
     * @brief Maps continuous virtual addresses to sequential physical frames inside a target tree.
     *
     * Dynamically handles multi-tier directory generation (L1 through L3) to resolve target structures. 
     * Supports standard 4KB terminal entries, 2MB huge blocks, and 1GB large blocks. 
     * Automatically invalidates TLB lines following a successful map.
     *
     * @param[in] root     Physical base address anchoring the target context directory root.
     * @param[in] virt     Base virtual starting target route coordinate. Must be aligned to pg_size.
     * @param[in] phys     Base physical source system destination address. Must be aligned to pg_size.
     * @param[in] pg_count Cumulative count tracking total continuous frames to link.
     * @param[in] pg_size  Granule target scaling flag parameters choosing page geometric dimensions.
     * @param[in] flags    Bitmask attributes containing architectural access configurations.
     *
     * @retval 0         Success. Translation boundaries defined.
     * @retval -EINVAL   Root is null, or addresses violate structural layout alignments.
     * @retval -EOVERFLOW Address math loops past virtual boundaries.
     * @retval -EEXIST   An active structural descriptor mapping is already registered at the destination.
     * @retval -ENOMEM   Failed to allocate intermediate page directory frames.
     */
    int (*map)(u64 root, u64 virt, u64 phys, u64 pg_count, enum page_size pg_size, enum mmu_flags flags);

    /**
     * @brief Drops virtual translations out of a target tree, releasing empty intermediate directories.
     *
     * Wipes descriptors at the target translation tier. After clearing an entry, it loops back 
     * through parent indices (`is_pt_empty`) to reclaim dead intermediate infrastructure pages via the PMM.
     *
     * @param[in] root     Physical base address anchoring the target context directory root.
     * @param[in] virt     Base virtual starting path boundary layout coordinate to strip away.
     * @param[in] pg_count Cumulative count tracking continuous allocations to detach.
     * @param[in] pg_size  Granule scale configuration tracking target mapping dimensions.
     *
     * @retval 0       Success. Translations erased and empty structural frames recycled.
     * @retval -EINVAL Specified root address is empty or uninitialized.
     */
    int (*unmap)(u64 root, u64 virt, u64 pg_count, enum page_size pg_size);

    /**
     * @brief Modifies permission attribute bitmasks over a range of active translations.
     *
     * Adjusts active architectural descriptor attributes (e.g., swapping Read/Write to Read-Only, 
     * toggling Execution access blocks) without breaking structural address bindings.
     *
     * @param[in] root     Physical base address anchoring the target context directory root.
     * @param[in] virt     Base virtual target coordinate mapping modification paths.
     * @param[in] pg_count Continuous count tracking total translation cells to update.
     * @param[in] pg_size  Granule size selection detailing targeted layout steps.
     * @param[in] flags    Modified attribute settings mask to apply over existing records.
     *
     * @retval 0       Success. Internal page properties altered cleanly.
     * @retval -EINVAL Target context root reference evaluated to null.
     * @retval -EFAULT Attempted modification of an unmapped or invalid virtual page address.
     */
    int (*protect)(u64 root, u64 virt, u64 pg_count, enum page_size pg_size, enum mmu_flags flags);

    /* =========================================================================
     * AREA 4: Translation Lookups and TLB Cache Consistency
     * ========================================================================= */

    /**
     * @brief Traverses the translation context tree to perform a manual software page walk.
     *
     * Manually walks intermediate pointers matching hardware table lookup rules. 
     * Decodes individual attributes and handles huge blocks to resolve physical address maps.
     *
     * @param[in]  root      Physical address matching context matrix tracking origins.
     * @param[in]  virt      Target lookup address space coordinates to translate.
     * @param[out] phys_out  Pointer capturing the parsed real physical system frame target address.
     * @param[out] flags_out Pointer capturing isolated AArch64 page configuration attribute bitmasks.
     *
     * @retval 0       Success; real coordinates written to outbound references.
     * @retval -EINVAL Missing critical reference targets or parameters.
     * @retval -EFAULT Traversal encountered an invalid translation path step or broken unmapped nodes.
     */
    int (*translate)(u64 root, u64 virt, u64 *phys_out, enum mmu_flags *flags_out);

    /**
     * @brief Flushes all cached translation records globally from the active core TLB array.
     *
     * Triggers full cache clearance mechanisms via systemic instructions (`tlbi vmalle1is`). 
     * Enforces strict execution ordering via barrier sequences (`dsb ish` followed by `isb`).
     *
     * @retval 0 Operation processed cleanly.
     */
    int (*flush)(void);

    /**
     * @brief Invalidates cached TLB addresses across targeted virtual allocations.
     *
     * Optimizes performance by choosing between two invalidation methods. If the requested frame volume 
     * crosses `TLB_BATCH_THRESHOLD`, it executes a global context flush. Otherwise, it extracts the current 
     * active ASID and steps iteratively through single address channels using `tlbi vae1is` or `tlbi vale1is`.
     *
     * @param[in] virt     Base starting virtual track location to remove from cached TLB structures.
     * @param[in] pg_count Cumulative continuous page array scale to evict.
     * @param[in] pg_size  Granule size attributes determining address stride lengths.
     *
     * @retval 0 Invalidation barriers confirmed.
     */
    int (*invalidate)(u64 virt, u64 pg_count, enum page_size pg_size);

    /**
     * @brief Sets system cache policies across execution platforms via attribute profiles.
     *
     * Directly modifies the hardware Memory Attribute Indirection Register configuration register space (`mair_el1`).
     * Defines indexing behaviors used by structural entries to control device or normal cached configurations.
     *
     * @param[in] mair Raw register configuration structure value tracking cache profile patterns.
     *
     * @retval 0 Operation locked and applied.
     */
    int (*set_mair)(u64 mair);

} mmu_interface_t;

#endif /* MMU_API_H */
