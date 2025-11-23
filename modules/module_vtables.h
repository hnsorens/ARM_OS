#include "module.h"

struct ppm_vtable_t;
struct vmm_vtable_t;

typedef struct ppm_vtable_t
{
  void (*ppm_init)(memory_region_t*, unsigned long);
  void (*set_vmm)(struct vmm_vtable_t*);
  void* (*alloc_virt_kernel)(unsigned long, unsigned long);
  void (*free_virt_kernel)(unsigned long, unsigned long);
  void* (*alloc_phys)(unsigned long);
  void (*free_phys)(unsigned long, unsigned long);
  unsigned long (*memory_available)();
} ppm_vtable_t;

typedef struct vmm_vtable_t
{
  void (*vmm_init)(ppm_vtable_t*, unsigned long);
  void (*pages_map_kernel)(unsigned long, unsigned long, unsigned long, unsigned long);
  unsigned long (*virt_to_phys_kernel)(unsigned long);
} vmm_vtable_t;