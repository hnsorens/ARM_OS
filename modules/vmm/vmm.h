#ifndef VMM_H
#define VMM_H

#include "../../include/api/mmu.h"
#include "../../include/api/vmm.h"

/* --- Linux-Style Architectural Virtual Boundary Configurations --- */
#define VMM_USER_SPACE_MIN           0x0000000000001000ULL 
#define VMM_USER_SPACE_MAX           0x00007FFFFFFFF000ULL 
#define VMM_DEFAULT_ALIGNMENT        4096

/* Modern Linux-inspired virtual memory area tracker structure */
typedef struct vm_area {
    u64          base;             /* Segment starting virtual address */
    u64           size;             /* Segment total size in bytes */
    enum mmu_flags      flags;            /* Node architectural MMU permissions */
    enum vmm_region_type type;             /* Memory backing categorization type */
    bool             is_paged;         /* Allocation physical backing state */
    struct vm_area  *next;             /* Singly linked forward sibling node */
    struct vm_area  *prev;             /* Singly linked reverse sibling node */
} vm_area_t;

/* Address space descriptor map containing tracking anchors */
typedef struct vmm_space {
    u64          page_table_root;  /* Associated level 0 table root physical address */
    vm_area_t       *mmap_cache;       /* Fast lookup translation reference descriptor */
    vm_area_t       *vma_head;         /* Baseline sorted doubly-linked VMA list header */
} vmm_space_t;

/* --- Exported Virtual Memory Manager Interface API --- */

/**
 * @brief Allocates an empty 4KB physical page table to serve as a user address space root.
 * @param out_table_root Destination storage pointer capturing the root table frame address.
 * @return int Execution confirmation status code.
 */
int vmm_space_create(u64 *out_table_root);

/**
 * @brief Destroys and cleans up nested page tables and tracked memory ranges recursively.
 * @param table_root Physical address pointer tracking the active target space frame.
 * @return int Execution confirmation status code.
 */
int vmm_space_destroy(u64 table_root);

/**
 * @brief Allocates an independent virtual memory node layout chunk tracking context.
 * @param root Target physical level 0 page descriptor mapping structure frame.
 * @param vaddr Bidirectional parameter supplying hint limits or capturing chosen paths.
 * @param sz Boundary block sizing limits requested for generation.
 * @param flags Specific target protection properties to apply.
 * @param type Operational memory profile backing classifications.
 * @return int Execution confirmation status code.
 */
int vmm_allocate(u64 root, u64 *vaddr, u64 sz, enum mmu_flags flags, enum vmm_region_type type);

/**
 * @brief Anchors explicit arbitrary reservation zones into specified virtual addresses.
 * @param root Target level 0 tracking configuration physical structure pointer.
 * @param vaddr Targeted coordinate path point where reserving logic activates.
 * @param sz Scope scale boundary sizing parameters.
 * @return int Execution confirmation status code.
 */
int vmm_reserve(u64 root, u64 vaddr, u64 sz);

/**
 * @brief Evicts memory backing sectors and reclaims structural allocation range configurations.
 * @param root Destination location tracking the parent directory frame entry context.
 * @param vaddr Baseline starting point where structural range trimming executes.
 * @param sz Overall footprint boundary width specifications.
 * @return int Execution confirmation status code.
 */
int vmm_free(u64 root, u64 vaddr, u64 sz);

/**
 * @brief Resizes an active mapped allocation range modifying underlying table links.
 * @param root Target level 0 tracking frame anchor pointer.
 * @param vaddr Starting reference boundary identifying targeted target space segments.
 * @param old_sz Original reference scale constraints footprint width parameters.
 * @param new_sz Target reference modification sizing boundary conditions.
 * @return int Execution confirmation status code.
 */
int vmm_resize(u64 root, u64 vaddr, u64 old_sz, u64 new_sz);

/**
 * @brief Configures direct physical-to-virtual hardware mappings bypasses.
 * @param root Base level 0 structure tracking directory target physical index.
 * @param v Destination address range pointer targeting specific execution blocks.
 * @param p Target raw layout resource destination block location coordinates.
 * @param sz Scale configuration scope limits tracking dimensions.
 * @param f Specific execution security profile variables to append.
 * @return int Execution confirmation status code.
 */
int vmm_map_external(u64 root, u64 v, u64 p, u64 sz, enum mmu_flags f);

/**
 * @brief Modifies operational mapping validation features across selected virtual ranges.
 * @param root Core physical location anchor configuration directory target reference.
 * @param vaddr Base starting layout path where alterations commence tracking parameters.
 * @param sz General execution scope scale limitations boundaries.
 * @param new_flags Safe modification access restriction updates to safely record.
 * @return int Execution confirmation status code.
 */
int vmm_protect(u64 root, u64 vaddr, u64 sz, enum mmu_flags new_flags);

/**
 * @brief Queries information metrics regarding specific active virtual mapping boundaries.
 * @param root Base entry pointer anchoring the parent page structure context maps.
 * @param vaddr Exact target line point coordinate structure query lookup link.
 * @param out_info Variable destination tracking block used to store metadata.
 * @return int Execution confirmation status code.
 */
int vmm_query(u64 root, u64 vaddr, struct vmm_region_info *out_info);

/**
 * @brief Sets active address translation profiles directly into structural context registers.
 * @param root Targeted physical directory tracking tree context physical base address.
 * @return int Execution confirmation status code.
 */
int vmm_activate(u64 root);

/**
 * @brief Synchronizes address ranges across processors flushing specialized entry tracks.
 * @param root Structural location identifying specific tracking map properties.
 * @param vaddr Selected coordinates targeting clear execution zones.
 * @param sz Size parameters configuring tracking boundaries.
 * @return int Execution confirmation status code.
 */
int vmm_sync(u64 root, u64 vaddr, u64 sz);

#endif
