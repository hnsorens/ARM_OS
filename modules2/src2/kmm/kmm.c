#include "kmm/kmm.h"
#include "kmm/kmm_impl.h"

#include "heap.h"
#include "slab.h"

#include "pmm/pmm_inc.h"
#include "str/str_inc.h"
#include "serial_debug/serial_debug_inc.h"

heap_t heap;

slab_allocator_t *slab_allocators[12];

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

void *ksalloc(int order) {
  if (order >= 12)
    return 0;

  return slab_alloc(slab_allocators[order]);
}

void ksfree(int order, void *addr) {
  if (order >= 12)
    return;

  slab_free(slab_allocators[order], addr);  
}    

override void kmm_fetch(core_ops *ops)
{
  pmm_fetch(ops);
  str_fetch(ops);
  serial_debug_fetch(ops);
}

override void kmm_start(core_ops *ops)
{
  DEBUG("Init");
  // pmm_vtable_t *ppm = (pmm_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_PMM);
  heap_init(&heap, 0x40000000000, 0x1000000);

  for (int i = 0; i < 12; i++) {
    slab_allocators[i] = create_slab_allocator(&heap, i, 0);
  }
  
  DEBUG("DONE");
}

override void kmm_init(kmm_ops* ops)
{
  ops->kfree = kfree;
  ops->kmalloc = kmalloc;
  ops->kcalloc = kcalloc;
  ops->krealloc = krealloc;
  ops->kmalloc_aligned = kmalloc_aligned;
  ops->ksalloc = ksalloc;
  ops->ksfree = ksfree;
}
