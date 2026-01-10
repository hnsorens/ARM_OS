
#include "module_types.h"
#include <stdint.h>

#ifndef MODULE_VTABLES_H
#define MODULE_VTABLES_H

#define vtable_def void (*init)(kernel_vtable_t*, virt_addr_t load);

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

typedef struct ppm_vtable_t
{
  vtable_def
  void* (*alloc_virt_kernel)(unsigned long, unsigned long);
  void (*free_virt_kernel)(unsigned long, unsigned long);
  void* (*alloc_phys)(unsigned long);
  void (*free_phys)(unsigned long, unsigned long);
  unsigned long (*memory_available)();
} ppm_vtable_t;

typedef struct vmm_vtable_t
{
  vtable_def
  void (*pages_map_kernel)(unsigned long, unsigned long, unsigned long, unsigned long);
  unsigned long (*virt_to_phys_kernel)(unsigned long);
} vmm_vtable_t;

typedef struct kmm_vtable_t
{
  vtable_def
  virt_addr_t (*kmalloc)(unsigned long);
  virt_addr_t (*kcalloc)(unsigned long, unsigned long);
  virt_addr_t (*krealloc)(virt_addr_t, unsigned long);
  void (*kfree)(virt_addr_t vaddr);
} kmm_vtable_t;

typedef struct gic_vtable_t
{
  vtable_def
  
} gic_vtable_t;

typedef struct ext2_vtable_t 
{
  vtable_def
  void* (*create_fs)(unsigned int, unsigned int);
} ext2_vtable_t;

typedef struct ide_vtable_t 
{
  vtable_def
  void* (*read_fn)(unsigned int, unsigned int);
  void (*write_fn)(unsigned int, unsigned int, void*);
} ide_vtable_t;

typedef struct bus_controller_vtable_t
{
  vtable_def
  uintptr_t (*find_device)(unsigned char id);
  void (*init_device)(unsigned long device_base);
} bus_controller_vtable_t;

typedef struct serial_vtable_t
{
  vtable_def
  int (*serial_printf)(const char*, ...);
} serial_vtable_t;

typedef struct libk_vtable_t
{
  vtable_def

} libk_vtable_t;

#endif
