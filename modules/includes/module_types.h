#ifndef MODULE_TYPE_H
#define MODULE_TYPE_H

#include "module_enum.h"

typedef unsigned long virt_addr_t;
typedef unsigned long phys_addr_t;

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


typedef struct kernel_vtable_t
{
  void (*init)(kernel_entry_t*);
  unsigned long (*find_module_vtable_by_type)(module_type_t type);
  unsigned long (*find_module_vtable_by_name)(char* name);
  unsigned long (*total_system_memory)();
  memory_region_t* (*memory_regions)();
  unsigned long (*memory_regions_count)();
} kernel_vtable_t;

typedef struct vtable_init_t
{
  void (*init)(kernel_vtable_t*, virt_addr_t load);
} vtable_init_t;

#endif