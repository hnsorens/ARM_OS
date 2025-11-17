#include "../module.h"

vtable(KernelVTable);
start(init);

typedef struct Module
{
  char moduleName[32];
  unsigned long size;
  void* base;
  void* vtable;
} Module;

typedef struct ModuleTable
{
  unsigned long size;
  Module* modules;
} ModuleTable;

typedef struct MemoryRegion
{
  unsigned long start;
  unsigned long size;
  enum
  {
    MEMORY_FREE,
    MEMORY_USED,
  } memory_type;
} MemoryRegion;

typedef struct kernel_entry_t
{
  ModuleTable module_table;
  MemoryRegion* memory_map;
  unsigned long total_memory;
  void* runtime_services;
} kernel_entry_t;

typedef struct KernelVTable
{
  void (*kernel_entry)(kernel_entry_t);
} KernelVTable;

void kernel_entry(kernel_entry_t entry)
{

  unsigned long test = 123;
  __asm__ volatile("mov x9, %0" :: "r"(test));

  while (1)
    ;
}

void init(KernelVTable* table)
{
  table->kernel_entry = kernel_entry;
}

