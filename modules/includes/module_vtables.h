
#include "module_types.h"
#include <stdint.h>

#ifndef MODULE_VTABLES_H
#define MODULE_VTABLES_H

#define vtable_def void (*fetch)(kernel_vtable_t*, virt_addr_t load); void (*init)(kernel_vtable_t*);

typedef struct pmm_vtable_t
{
  vtable_def
  void* (*alloc_virt_kernel)(unsigned long, unsigned long);
  void (*free_virt_kernel)(unsigned long, unsigned long);
  void* (*alloc_phys)(unsigned long);
  void (*free_phys)(unsigned long, unsigned long);
  unsigned long (*memory_available)();
} pmm_vtable_t;

typedef struct vmm_vtable_t
{
  vtable_def
  void (*pages_map_kernel)(unsigned long, unsigned long, unsigned long, unsigned long);
  unsigned long (*virt_to_phys_kernel)(unsigned long);
} vmm_vtable_t;

typedef struct kmm_vtable_t
{
  vtable_def
  void* (*kmalloc)(unsigned long);
  void* (*kcalloc)(unsigned long, unsigned long);
  void* (*krealloc)(void*, unsigned long);
  void (*kfree)(void* vaddr);
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

typedef struct serial_debug_vtable_t
{
  vtable_def
  int (*serial_printf)(char*, ...);
} serial_debug_vtable_t;

typedef struct libk_vtable_t
{
  vtable_def

} libk_vtable_t;

typedef struct blk_dev_vtable_t
{
  vtable_def
  void* (*read_sectors)(uint32_t lba, uint32_t sector_count);
  void  (*write_sectors)(uint32_t lba, uint32_t sector_count, void* data);
} blk_dev_vtable_t;

typedef struct gpt_vtable_t
{
  vtable_def
  

} gpt_vtable_t;

typedef struct str_vtable_t
{
  vtable_def
  void* (*memset)(void* s, int c, unsigned long n);
  void* (*memcpy)(void* dest, const void* src, unsigned long n);
  void* (*memmove)(void* dest, const void* src, unsigned long n);
  int (*memcmp)(const void* s1, const void* s2, unsigned long n);
  void* (*memchr)(const void* s, int c, unsigned long n);

  unsigned long (*strlen)(const char* s);
  char* (*strcpy)(char* dest, const char* src);
  char* (*strncpy)(char* dest, const char* src, unsigned long n);
  char* (*strcat)(char* dest, const char* src);
  char* (*strncat)(char* dest, const char* src, unsigned long n);
  int (*strcmp)(const char* s1, const char* s2);
  int (*strncmp)(const char* s1, const char* s2, unsigned long n);
  char* (*strchr)(const char* s, int c);
  char* (*strrchr)(const char* s, int c);
  char* (*strstr)(const char* haystack, const char* needle);

  char* (*strdup)(const char* s);

} str_vtable_t;

#endif
