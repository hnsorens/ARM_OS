#include "mmu/mmu_impl.h"

#include "mmu/mmu_types.h"
#include "page_table.h"

#include "pmm/pmm_inc.h"
#include "serial_debug/serial_debug_inc.h"

page_table_t kernel_page_table;

int pages_map_kernel(vaddr_t vaddr, paddr_t paddr, size_t sz, prot_t prot)
{
    // fix later
    uint32_t page_count = sz / 4096;
    pages_map(&kernel_page_table, vaddr, paddr, 0, page_count);
    return 1;
}

paddr_t virt_to_phys_kernel(vaddr_t va)
{
    return virt_to_phys(kernel_page_table, va);
}

override void mmu_fetch(core_ops *ops)
{
    pmm_fetch(ops);
    serial_debug_fetch(ops);
}

override void mmu_start(core_ops* ops)
{
    DEBUG("Init");
    // Creates initial kernel page table
    unsigned long total_memory = ops->total_system_memory();
    kernel_page_table = pages_create_identity_page_table(total_memory);
    
    page_enable(kernel_page_table);
}

override void mmu_init(mmu_ops *ops)
{
    ops->map_kernel = pages_map_kernel;
    ops->v2p_kernel = virt_to_phys_kernel;
}