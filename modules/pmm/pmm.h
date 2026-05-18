#ifndef PMM_H
#define PMM_H

#include <stddef.h>
#include <stdint.h>
#include "../../include/type.h"
#include "../boot_info.h"

#define PMM_MAX_ORDER     16     /* 4KB * (2^15) = 128MB max contiguous block allocation */
#define PMM_PAGE_SIZE     4096   /* Architectural Base Page Size */
#define PMM_PAGE_MASK     (~0xFFFULL)

typedef struct pmm_page_meta
{
    uint32_t ref_count;
    uint8_t  order;
    uint8_t  is_free;
    uint16_t flags;
} pmm_page_meta_t;

typedef struct pmm_block_node
{
    struct pmm_block_node *next;
    struct pmm_block_node *prev;
} pmm_block_node_t;

typedef struct pmm_order_list
{
    pmm_block_node_t *head;
    size_t            block_count;
} pmm_order_list_t;

typedef struct pmm_allocator
{
    pmm_order_list_t orders[PMM_MAX_ORDER];
    size_t           total_memory_bytes;
    size_t           free_memory_bytes;
} pmm_allocator_t;

/* --- Core Initialization System Interface --- */
void pmm_init(memory_region_t *memory_map, size_t region_count, paddr_t hhdm_offset);

/* --- High-Level Global Interface Methods --- */
k_status_t pmm_alloc_page(uint8_t page_order, paddr_t *out_frame);
k_status_t pmm_free_page(uint8_t page_order, paddr_t frame);
k_status_t pmm_alloc_aligned(size_t count, size_t alignment, paddr_t *out);
k_status_t pmm_alloc_in_range(size_t count, paddr_t max_addr, paddr_t *out);

void pmm_retain(paddr_t frame);
void pmm_release(paddr_t frame);

size_t pmm_get_total_memory(void);
size_t pmm_get_free_memory(void);
k_status_t pmm_reserve_range(paddr_t start, size_t sz);

#endif
