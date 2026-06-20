/**
 * @file vmm.h
 * @brief Virtual Memory Manager Interface API
 * * Implements an address space context management subsystem using an intrusive
 * Binary Search Tree (BST) layout to track Virtual Memory Areas (VMAs). Supports
 * dynamic page allocation, unmapped gap detection, range reservation, permission
 * protection changes, and runtime layout fragmentation/splitting.
 */

#ifndef VMM_H
#define VMM_H

#include <api/mmu.h>
#include <api/vmm.h>

/* --- Core Configuration Constraints --- */
#define VMM_USER_SPACE_MIN           0x0000000000001000ULL  /**< Canonical architectural minimum address boundary for user tasks */
#define VMM_USER_SPACE_MAX           0x00007FFFFFFFF000ULL  /**< Canonical architectural maximum address boundary for user tasks */
#define VMM_DEFAULT_ALIGNMENT        4096                   /**< Base hardware memory management page alignment unit size (4KB) */

/* Mock type define for spinlock tracking. Map this to your kernel's lock mechanism. */
typedef u32 spinlock_t;

/**
 * @struct boot_region
 * @brief Static tracking descriptor mapping virtual ranges pre-established by early bootstrap loaders.
 */
typedef struct boot_region {
    u64 base;       /**< The starting virtual address of the region */
    u64 size;       /**< The total size of the region in bytes */
    u32 flags;      /**< Hardware MMU page execution/write permissions (e.g., MMU_READ) */
    u32 type;       /**< Semantic purpose of the zone (e.g., VMM_REGION_CODE, VMM_REGION_STACK) */
} boot_region_t;

/**
 * @struct vm_area
 * @brief Intrusive tracking node mapping a single virtual memory allocation segment.
 * * VMAs are structured in a binary search tree topology sorted by base virtual address.
 * * @note To retain API structural compatibility, 'next' and 'prev' fields are utilized
 * internally as pointers to the RIGHT and LEFT children nodes within the BST.
 */
typedef struct vm_area {
    u64                  base;      /**< Segment starting virtual address (Page Aligned) */
    u64                  size;      /**< Segment total size in bytes (Multiple of 4KB) */
    enum mmu_flags       flags;     /**< Node architectural hardware MMU page permission flags */
    enum vmm_region_type type;      /**< Memory backing category identifier (e.g., Anonymous, MMIO, Guard) */
    bool                 is_paged;  /**< Flag indicating if physical frames are actively mapped to the range */
    bool                 in_use;    /**< Internal slab allocator availability lifecycle tracking state flag */
    struct vm_area       *next;     /**< Tree Pointer Alias: RIGHT child node reference path in the BST */
    struct vm_area       *prev;     /**< Tree Pointer Alias: LEFT child node reference path in the BST */
} vm_area_t;

/**
 * @struct vmm_space
 * @brief Master tracking instance block wrapping a discrete process virtual address space.
 * * Encapsulates the hardware page table tree and structural indexing boundaries for an isolated execution context.
 */
typedef struct vmm_space {
    u64              page_table_root; /**< Physical base address of the Level 0 (PML4/PGD) root page table directory */
    vm_area_t        *mmap_cache;     /**< Fast lookup temporal translation reference descriptor cache slot pointer */
    vm_area_t        *vma_head;       /**< Root entry node pointer anchoring the balanced virtual memory region BST */
    spinlock_t       lock;            /**< Local mutual exclusion synchronization primitive guarding internal tree modifications */
    struct vmm_space *next;           /**< Traversal link reference joining active contexts into a global registry chain */
    bool             in_use;          /**< Intrusive lifecycle state descriptor allocation tracking token flag */
} vmm_space_t;

/* --- Public API Operations Kernel Interfaces --- */

/**
 * @brief Clears global space registries and bootstraps the primary kernel virtual map environment.
 * * @param[in]  boot_pt_root Physical root address of the early bootstrap page directory structure.
 * @param[in]  regions      Array of static pre-mapped core memory zones established during startup.
 * @param[in]  region_count Element size count of the configuration boot array.
 * @return 0 on success, or appropriate error token code (e.g., EINVAL, ENOMEM).
 */
int vmm_init(u64 boot_pt_root, const boot_region_t *regions, int region_count);

/**
 * @brief Allocates an isolated address space context and provisions an underlying hardware page table root.
 * * @param[out] out_table_root Destination reference storing the newly generated Level 0 page directory root handle.
 * @return 0 on success, or error status code on hardware allocation failures.
 */
int vmm_space_create(u64 *out_table_root);

/**
 * @brief Iterates through and completely dismantling a virtual address context, freeing its page tables and VMA maps.
 * * @param[in]  table_root   The unique physical base handle identifier of the context to dismantle.
 * @return 0 on success, or EINVAL if arguments fail validation bounds.
 */
int vmm_space_destroy(u64 table_root);

/**
 * @brief Carves out a virtual memory segment and assigns physical frame page backings to it.
 * * Automatically searches for unallocated address holes if the provided address hint cannot be locked in.
 * * @param[in]  root         Physical root address of the targeting execution context directory map.
 * @param[inout] vaddr        Requested address location hint pointer; overwritten with the chosen allocation base.
 * @param[in]  sz           Total size requirement parameters footprint bytes requested.
 * @param[in]  flags        Hardware MMU execution/read/write permissions modifier rules.
 * @param[in]  type         Semantic classification identifier tying purpose to the block tracking logic.
 * @return 0 on success, ENOMEM on system frame depletion, or EINVAL on invalid inputs.
 */
int vmm_allocate(u64 root, u64 *vaddr, u64 sz, enum mmu_flags flags, enum vmm_region_type type);

/**
 * @brief Reserves a contiguous virtual address block without backing it with physical frames.
 * * Enforces rigid placement rules; ideal for setting unmapped thread boundaries or stack guard zones.
 * * @param[in]  root         Physical root address of the targeting execution context directory map.
 * @param[in]  vaddr        Strict virtual target location address to acquire.
 * @param[in]  sz           Total footprint window size parameters in bytes.
 * @return 0 on success, EEXIST if a collision occurs, or ENOMEM on tracker resource faults.
 */
int vmm_reserve(u64 root, u64 vaddr, u64 sz);

/**
 * @brief Releases virtual pages, clears hardware tables, and mutates tracking structures.
 * * Supports middle punch-out operations, auto-fragmenting a uniform tracking node into multiple descendant blocks.
 * * @param[in]  root         Physical root address of the targeting execution context directory map.
 * @param[in]  vaddr        Virtual starting coordinate address of the segment map layer to drop.
 * @param[in]  sz           Capacity size configuration dimension footprint in bytes.
 * @return 0 on success, or EINVAL if the target range does not align within a single valid VMA.
 */
int vmm_free(u64 root, u64 vaddr, u64 sz);

/**
 * @brief Modifies the capacity parameters of an active allocation block in-place.
 * * Contracts trailing limits smoothly or expands layout parameters linearly past their old threshold lines.
 * * @param[in]  root         Physical root address of the targeting execution context directory map.
 * @param[in]  vaddr        Virtual starting coordinate address tracking the targeted VMA.
 * @param[in]  old_sz       Previous size tracking parameter currently attributed to the block window.
 * @param[in]  new_sz       Revised size parameters boundary bounds requested.
 * @return 0 on success, or ENOMEM if expanding paths run into neighboring allocation blocks.
 */
int vmm_resize(u64 root, u64 vaddr, u64 old_sz, u64 new_sz);

/**
 * @brief Maps an unbacked virtual coordinate layer directly to an absolute peripheral MMIO physical range.
 * * Bypasses anonymous tracking layers entirely to interface structural components straight to peripheral targets.
 * * @param[in]  root         Physical root address of the targeting execution context directory map.
 * @param[in]  v            Target page-aligned virtual coordinate mapping point.
 * @param[in]  p            Target raw peripheral hardware physical core address mapping point.
 * @param[in]  sz           Total segment footprint size specifications parameters in bytes.
 * @param[in]  f            Hardware MMU architectural attribute configuration bit masks.
 * @return 0 on success, EEXIST on overlap faults, or status code variations from the paging engine.
 */
int vmm_map_external(u64 root, u64 v, u64 p, u64 sz, enum mmu_flags f);

/**
 * @brief Alters access flags across a tracking range, dynamically splitting tree structures as needed.
 * * If partial fields split access traits, the single VMA fragments into separate distinct left/mid/right nodes.
 * * @param[in]  root         Physical root address of the targeting execution context directory map.
 * @param[in]  vaddr        Virtual starting coordinate address of the target window to re-protect.
 * @param[in]  sz           Footprint size specifications parameters to adjust in bytes.
 * @param[in]  new_flags    The revised structural hardware MMU access flags block layout to inject.
 * @return 0 on success, ENOMEM on structure resource allocation faults, or EINVAL on validation errors.
 */
int vmm_protect(u64 root, u64 vaddr, u64 sz, enum mmu_flags new_flags);

/**
 * @brief Inspects active indexing tracking maps and writes matching record attributes into diagnostic structures.
 * * @param[in]  root         Physical root address of the targeting execution context directory map.
 * @param[in]  vaddr        Target coordinate address point being validated.
 * @param[out] out_info     Destination reference struct tracking harvested snapshot record metrics.
 * @return 0 on success, or EINVAL if no mapping encapsulates the targeted location.
 */
int vmm_query(u64 root, u64 vaddr, struct vmm_region_info *out_info);

/**
 * @brief Binds a selected virtual memory environment to the current hardware execution engine core context.
 * * @param[in]  root         The page directory physical context root token layout to register (CR3 translation path).
 * @return 0 on success, or hardware activation status error tokens.
 */
int vmm_activate(u64 root);

/**
 * @brief Forces a translation layout synchronization pass across the CPU cores to purge obsolete cached paths.
 * * @param[in]  root         Physical root address of the targeting execution context directory map.
 * @param[in]  vaddr        Target virtual address marking the start boundary line to synchronize.
 * @param[in]  sz            Footprint window size parameter to evaluate in bytes.
 * @return 0 on success, or status verification faults tracking individual invalidation requests.
 */
int vmm_sync(u64 root, u64 vaddr, u64 sz);

#endif /* VMM_H */
