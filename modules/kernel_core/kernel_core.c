#include "../module.h"
#include "../module_vtables.h"
#include "../module_debug.h"

vtable(kernel_vtable_t);
start(init);

typedef struct module_t
{
  char moduleName[32];
  unsigned long size;
  void* base;
  void* vtable;
} Module;

typedef struct module_table_t
{
  unsigned long size;
  Module* modules;
} module_table_t;

typedef struct kernel_entry_t
{
  module_table_t module_table;
  memory_region_t *memory_map;
  unsigned long memory_map_entry_count;
  unsigned long total_memory;
  void* runtime_services;
} kernel_entry_t;

typedef struct kernel_vtable_t
{
  void (*kernel_entry)(kernel_entry_t);
} kernel_vtable_t;

void kernel_entry(kernel_entry_t entry)
{
  ((ppm_vtable_t*)(entry.module_table.modules[1].vtable))->ppm_init(entry.memory_map, entry.memory_map_entry_count);
  ((vmm_vtable_t*)(entry.module_table.modules[2].vtable))->vmm_init((ppm_vtable_t*)(entry.module_table.modules[1].vtable), 17179869184);
  ((ppm_vtable_t*)(entry.module_table.modules[1].vtable))->set_vmm((vmm_vtable_t*)(entry.module_table.modules[2].vtable));
}

void init(kernel_vtable_t *table)
{
  table->kernel_entry = kernel_entry;
}

