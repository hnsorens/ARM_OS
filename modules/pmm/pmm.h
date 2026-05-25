#ifndef PMM_H
#define PMM_H

#include "../boot_info.h"

#define PMM_MAX_ORDER     64     /* 4KB * (2^15) = 128MB max contiguous block allocation */
#define PMM_PAGE_SIZE     4096   /* Architectural Base Page Size */
#define PMM_PAGE_MASK     (~0xFFFULL)

typedef struct pmm_page_meta
{
    u32 ref_count;
    u8  order;
    u8  is_free;
    u16 flags;
} pmm_page_meta_t;

typedef struct pmm_block_node
{
    struct pmm_block_node *next;
    struct pmm_block_node *prev;
} pmm_block_node_t;

typedef struct pmm_order_list
{
    pmm_block_node_t *head;
    u64            block_count;
} pmm_order_list_t;

typedef struct pmm_allocator
{
    pmm_order_list_t orders[PMM_MAX_ORDER];
    u64           total_memory_bytes;
    u64           free_memory_bytes;
} pmm_allocator_t;

/* --- Core Initialization System Interface --- */
void pmm_init(memory_region_t *memory_map, u64 region_count, u64 hhdm_offset);

/* --- High-Level Global Interface Methods --- */
int pmm_alloc_page(u8 page_order, u64 *out_frame);
int pmm_alloc_aligned(u64 count, u64 alignment, u64 *out);
int pmm_alloc_in_range(u64 count, u64 max_addr, u64 *out);

int pmm_retain(u64 frame);
int pmm_release(u64 frame);

u64 pmm_get_total_memory(void);
u64 pmm_get_free_memory(void);
int pmm_reserve_range(u64 start, u64 sz);

#endif
