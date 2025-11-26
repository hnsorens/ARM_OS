
#include "buddy.h"

#include "../module_debug.h"

vtable(ppm_vtable_t);
start(init, ppm_init);

buddy_allocator_t allocator;
vmm_vtable_t* vmm = 0;

unsigned long calculate_total_memory(memory_region_t* regions, unsigned long region_count)
{
  unsigned long total_memory = 0;
  for (int i = 0; i < region_count; i++)
  {
    total_memory += regions[i].size;
  }
  return total_memory * 4096;
}

void ppm_init(kernel_vtable_t* kvtable)
{
  vmm = (vmm_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_VMM);

  unsigned long region_count = kvtable->memory_regions_count();
  memory_region_t* regions = kvtable->memory_regions();

  unsigned long total_memory = calculate_total_memory(regions, region_count);

  unsigned long buddy_allocator_size = buddy_get_memory_size(total_memory);
  unsigned long buddy_allocator_page_count = (buddy_allocator_size / 4096) + 1;

  unsigned long buddy_memory = 0;
  for (int i = 0; i < region_count; i++)
  {
    if (regions[i].size > buddy_allocator_page_count && regions[i].memory_type == MEMORY_FREE)
    {
      buddy_memory = regions[i].start;
      regions[i].size -= buddy_allocator_page_count;
      regions[i].start += 4096 * buddy_allocator_page_count;
      break;
    }
  }

  buddy_init(regions, region_count, &allocator, buddy_memory, total_memory);
}

void* alloc_phys(unsigned long order)
{
  return (void*)buddy_alloc_phys(&allocator, order);
}

void free_phys(unsigned long addr, unsigned long order)
{
  buddy_free_phys(&allocator, addr, order);
}

void set_vmm(vmm_vtable_t* vmm_vtable)
{
  vmm = vmm_vtable;
}

void* page_alloc_kernel(unsigned long virt_addr, unsigned long size)
{
  return buddy_alloc_kernel(&allocator, virt_addr, size, vmm);
}

void page_free_kernel(unsigned long virt_addr, unsigned long size)
{
  return buddy_free_kernel(&allocator, virt_addr, size, vmm);
}

unsigned long memory_available()
{
  return buddy_memory_available(&allocator);
}

void init(ppm_vtable_t *vtable)
{
  vtable->alloc_virt_kernel = page_alloc_kernel;
  vtable->free_virt_kernel = page_free_kernel;
  vtable->alloc_phys = alloc_phys;
  vtable->free_phys = free_phys;
  vtable->memory_available = memory_available;
}