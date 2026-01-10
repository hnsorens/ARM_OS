#ifndef MODULE_TYPE_H
#define MODULE_TYPE_H

typedef unsigned long virt_addr_t;
typedef unsigned long phys_addr_t;

typedef enum module_type_t
{
  MODULE_KERNEL_CORE,
  MODULE_VMM,
  MODULE_PMM,
  MODULE_KMM,
  MODULE_GIC,
  MODULE_SERIAL_DEBUG,
  MODULE_BUS_CONTROLLER,
} module_type_t;

typedef struct module_t
{
  char moduleName[32];
  unsigned long size;
  void* base;
  void* vtable;
  module_type_t type;
} Module;

typedef struct module_table_t
{
  unsigned long size;
  Module* modules;
} module_table_t;

typedef enum memory_type_t
{
  MEMORY_UNKNOWN,
  MEMORY_FREE,
  MEMORY_USED,
} memory_type_t;

typedef struct memory_region_t
{
  unsigned long start;
  unsigned long size;
  memory_type_t memory_type;
} memory_region_t;

typedef struct kernel_entry_t
{
  module_table_t module_table;
  memory_region_t *memory_map;
  unsigned long memory_map_entry_count;
  unsigned long total_memory;
  void* runtime_services;
} kernel_entry_t;

#endif