/**
 * @file pmm.h
 * @brief Public Architectural Interface and Structure Layouts for the Physical Memory Manager (PMM).
 */

#ifndef PMM_H
#define PMM_H

#include <boot_info.h>

#define PMM_MAX_ORDER      64    
#define PMM_PAGE_SIZE      4096  
#define PMM_PAGE_MASK      (~0xFFFULL)

/**
 * @struct pmm_page_meta
 * @brief Structural metadata descriptor mapping a physical page granule tracking context.
 */
typedef struct pmm_page_meta
{
    u32 ref_count; /**< Active retention reference tracker for tracking cross-shares */
    u8  order;     /**< Power-of-two block layout scale assignment metric */
    u8  is_free;   /**< Allocation state boolean flag identifier */
    u16 flags;     /**< Subsystem configuration status control flags bitmask */
} pmm_page_meta_t;

/**
 * @struct pmm_block_node
 * @brief Circular intrusive list tracking node layered inside unallocated page regions.
 */
typedef struct pmm_block_node
{
    struct pmm_block_node *next; /**< Direct linkage tracking forward pointer element */
    struct pmm_block_node *prev; /**< Backward reverse traversal tracking element link */
} pmm_block_node_t;

/**
 * @struct pmm_order_list
 * @brief Vector bucket header anchoring individual size-tiered page block elements.
 */
typedef struct pmm_order_list
{
    pmm_block_node_t *head;     /**< Head list pointer node tracking target pools */
    u64              block_count; /**< Cumulative block count available at current order level */
} pmm_order_list_t;

/**
 * @struct pmm_allocator
 * @brief Core global hardware allocator configuration metric and array state wrapper.
 */
typedef struct pmm_allocator
{
    pmm_order_list_t orders[PMM_MAX_ORDER]; /**< Tiered lists segregating buddy groups */
    u64              total_memory_bytes;    /**< Gross systemic memory registered payload */
    u64              free_memory_bytes;     /**< Instantaneous net unallocated structural payload */
} pmm_allocator_t;


/* --- Exported Physical Memory Subsystem Public API --- */

/**
 * @brief Boots up the structural allocator subsystem by tracking physical memory segments.
 * @param[in] memory_map  Pointer to physical region array structures supplied by the loader environment.
 * @param[in] region_count Volume limit specifying the entries present inside target structure.
 * @param[in] hhdm_offset Higher-Half Direct Map address scaling linear tracking criteria space.
 */
void pmm_init(memory_region_t *memory_map, u64 region_count, u64 hhdm_offset);

/**
 * @brief Allocates an isolated block layout of size power-of-two page orders.
 * @param[in]  page_order Power-of-two scale sizing profile selector constraint.
 * @param[out] out_frame  Destination placeholder mapping to the generated physical base memory address.
 * @return int Operational execution status context values.
 */
int pmm_alloc_page(u8 page_order, u64 *out_frame);

/**
 * @brief Allocates block maps optimized to line up perfectly against precise alignment boundaries.
 * @param[in]  count     Total contiguous page chunks requested to allocate.
 * @param[in]  alignment Alignment scale filter constraint (Must be a power-of-two value).
 * @param[out] out       Destination parameter targeting generated physical memory addresses.
 * @return int Operational execution status context values.
 */
int pmm_alloc_aligned(u64 count, u64 alignment, u64 *out);

/**
 * @brief Searches and claims safe memory tracks confined within an absolute physical ceiling.
 * @param[in]  count    Total contiguous page allocations to lock into target storage tracking blocks.
 * @param[in]  max_addr Absolute physical cap upper limit ceiling marker (Non-inclusive constraint).
 * @param[out] out      Destination storage pointer matching structural target assignments.
 * @return int Operational execution status context values.
 */
int pmm_alloc_in_range(u64 count, u64 max_addr, u64 *out);

/**
 * @brief Increases the internal tracking reference calculation for a target physical frame block.
 * @param[in] frame Target memory alignment base frame tracking physical address string coordinate.
 * @return int Operational execution status context values.
 */
int pmm_retain(u64 frame);

/**
 * @brief Drops retention markers across target frame sets, returning blocks to pools when empty.
 * @param[in] frame Target memory tracking path frame physical start point configuration token.
 * @return int Operational execution status context values.
 */
int pmm_release(u64 frame);

/**
 * @brief Reports on total byte configurations mapped to physical regions handled by the PMM core.
 * @return u64 Accumulation total integer representing available systemic space allocations.
 */
u64 pmm_get_total_memory(void);

/**
 * @brief Reports instantly available byte capacities remaining across active pool layers.
 * @return u64 Tracked residual raw volume integer fields.
 */
u64 pmm_get_free_memory(void);

/**
 * @brief Permanently isolates explicit physical regions away from the allocation tracking lines.
 * @param[in] start Target physical base coordinate anchoring memory reservation boundaries.
 * @param[in] sz    Volumetric specification width detailing total byte structures to isolate.
 * @return int Operational execution status context values.
 */
int pmm_reserve_range(u64 start, u64 sz);

#endif
