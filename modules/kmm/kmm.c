

#include "../module_vtables.h"
#include "../module_types.h"
#include "../module_debug.h"
#include "heap.h"

vtable(kmm_vtable_t);
start(init, kmm_init);

heap_t heap;

virt_addr_t kmalloc(unsigned long size)
{
  return heap_malloc(&heap, size);
}

virt_addr_t kcalloc(unsigned long num, unsigned long size)
{
  return heap_calloc(&heap, num, size);
}

virt_addr_t krealloc(virt_addr_t ptr, unsigned long new_size)
{
  return heap_realloc(&heap, ptr, new_size);
}

void kfree(virt_addr_t ptr)
{
  heap_free(&heap, ptr);
}

void kmm_init(kernel_vtable_t* kvtable, virt_addr_t load)
{
  ppm_vtable_t *ppm = (ppm_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_PMM);
  heap_init(&heap, ppm, 0x40000000000, 0x100000);
}

void init(kmm_vtable_t* vtable)
{
  vtable->kfree = kfree;
  vtable->kmalloc = kmalloc;
  vtable->kcalloc = kcalloc;
  vtable->krealloc = krealloc;
}