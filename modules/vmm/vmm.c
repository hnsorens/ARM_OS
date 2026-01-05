#include "../module_debug.h"
#include "../module_vtables.h"

#include "page_table.h"

vtable(vmm_vtable_t);
start(init, vmm_init);

ppm_vtable_t* ppm;
page_table_t kernel_page_table;

void vmm_init(kernel_vtable_t* kvtable, virt_addr_t load)
{

    // Finds the Physical Memory Manager
    ppm = (ppm_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_PMM);

    // Creates initial kernel page table
    unsigned long total_memory = kvtable->total_system_memory();
    kernel_page_table = pages_create_identity_page_table(ppm, total_memory);
    
    page_enable(kernel_page_table);
}

void pages_map_kernel(virt_addr_t virtual_address, phys_addr_t physical_address, unsigned long page_order, unsigned long page_count)
{
    pages_map(&kernel_page_table, virtual_address, physical_address, page_order, page_count, ppm);
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