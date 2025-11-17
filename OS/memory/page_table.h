#ifndef PAGE_TABLE_H
#define PAGE_TABLE_H

#include <stddef.h>
#include <stdint.h>

typedef struct page_table_indices_t
{
    uint16_t p0_index;
    uint16_t p1_index;
    uint16_t p2_index;
    uint16_t p3_index;
    uint16_t offset;
} page_table_indices_t;

typedef uint64_t* page_table_t;

typedef struct
{
    uint64_t entry;
    uint64_t size;
} page_lookup_result_t;

typedef enum page_size_t
{
  PAGE_SIZE_4KB = 0x1000,
  PAGE_SIZE_2MB = 0x200000,
  PAGE_SIZE_1GB = 0x40000000,
} page_size_t;

#define P0_INDEX(x) (((x) >> 39) & 0x1FF)
#define P1_INDEX(x) (((x) >> 30) & 0x1FF)
#define P2_INDEX(x) (((x) >> 21) & 0x1FF)
#define P3_INDEX(x) (((x) >> 12) & 0x1FF)

typedef uintptr_t phys_addr_t;
typedef uintptr_t virt_addr_t;

page_table_indices_t extract_indices(uint64_t virtual_address);

void page_table_add_pages(page_table_t* page_table, virt_addr_t virt, phys_addr_t phys, page_size_t page_size, size_t page_count);
size_t get_kernel_page_table_page_count(size_t total_memory);
void create_kernel_page_table(phys_addr_t phys);

#endif