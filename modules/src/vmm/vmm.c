#include "module.h"

#include "page_table.h"

#include "modules/pmm.h"

#define debug "VMM"

vtable(vmm_vtable_t);
start(init, vmm_fetch, vmm_init);

page_table_t kernel_page_table;

void vmm_fetch(kernel_vtable_t *kvtable)
{
    DEBUG("Fetch");
    pmm_fetch(kvtable);
}

void vmm_init(kernel_vtable_t* kvtable)
{
    DEBUG("Init");
    // Creates initial kernel page table
    unsigned long total_memory = kvtable->total_system_memory();
    kernel_page_table = pages_create_identity_page_table(total_memory);
    
    page_enable(kernel_page_table);
}

void pages_map_kernel(virt_addr_t virtual_address, phys_addr_t physical_address, unsigned long page_order, unsigned long page_count)
{
    pages_map(&kernel_page_table, virtual_address, physical_address, page_order, page_count);
}

phys_addr_t virt_to_phys_kernel(virt_addr_t virtual_address)
{
    return virt_to_phys(kernel_page_table, virtual_address);
}

void init(vmm_vtable_t *vtable)
{
    vtable->pages_map_kernel = pages_map_kernel;
    vtable->virt_to_phys_kernel = virt_to_phys_kernel;
}