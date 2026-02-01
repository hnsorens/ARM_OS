
#include "module.h"
#include "heap.h"

#include "modules/vtables/kmm.h"

#include "modules/pmm.h"
#include "modules/str.h"
#include "modules/serial_debug.h"

#define debug "KMM"

vtable(kmm_vtable_t);
start(init, kmm_fetch, kmm_init);

heap_t heap;

void* kmalloc(unsigned long size)
{
  return (void*)heap_malloc(&heap, size);
}

void* kcalloc(unsigned long num, unsigned long size)
{
  return (void*)heap_calloc(&heap, num, size);
}

void* krealloc(void* ptr, unsigned long new_size)
{
  return (void*)heap_realloc(&heap, (unsigned long)ptr, new_size);
}

void kfree(void* ptr)
{
  heap_free(&heap, (unsigned long)ptr);
}

void* kmalloc_aligned(unsigned long size, unsigned long alignment)
{
  return (void*)heap_malloc_aligned(&heap, size, alignment);
}

void kmm_fetch(kernel_vtable_t *kvtable)
{
  pmm_fetch(kvtable);
  str_fetch(kvtable);
  serial_debug_fetch(kvtable);
}

void kmm_init(kernel_vtable_t *kvtable)
{
  DEBUG("Init");
  // pmm_vtable_t *ppm = (pmm_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_PMM);
  heap_init(&heap, 0x40000000000, 0x1000000);
  DEBUG("DONE");
}

void init(kmm_vtable_t* vtable)
{
  vtable->kfree = kfree;
  vtable->kmalloc = kmalloc;
  vtable->kcalloc = kcalloc;
  vtable->krealloc = krealloc;
  vtable->kmalloc_aligned = kmalloc_aligned;
}