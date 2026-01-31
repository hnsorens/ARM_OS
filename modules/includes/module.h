#ifndef MODULE_H
#define MODULE_H

#define DEBUG(fmt, ...) if (debug_serial) debug_serial->serial_printf("[" debug "] " fmt "\n", ##__VA_ARGS__)
#define ERROR(fmt, ...) if (debug_serial) debug_serial->serial_printf("ERROR: [" debug "] " fmt "\n", ##__VA_ARGS__)

#define __MAIN__

#define vtable(type) struct type ___table;
#define start(func_vtable_init, func_fetch, func_init)        \
void func_vtable_init(typeof(___table)* table);   \
void func_init(kernel_vtable_t* vtable);         \
void func_fetch(kernel_vtable_t* vtable);         \
virt_addr_t __load_addr__ = 0;                    \
static serial_debug_vtable_t* debug_serial = 0;         \
void ___fetch(kernel_vtable_t* vtable, virt_addr_t load)   \
{                                                         \
  __load_addr__ = load;                                   \
  debug_serial = (serial_debug_vtable_t*)vtable->find_module_vtable_by_type(MODULE_SERIAL_DEBUG); \
  func_fetch(vtable);                                      \
}                                                         \
__attribute__((section(".text._entry")))          \
typeof(___table)* _entry()                        \
{                                                 \
  func_vtable_init(&___table);                    \
  ___table.init = func_init;                      \
  ___table.fetch = ___fetch;  \
  return &___table;                               \
}

#define kernel_start(func_vtable_init, func_init) \
void func_vtable_init(typeof(___table)* table);   \
void func_init(struct kernel_entry_t* vtable);    \
__attribute__((section(".text._entry")))          \
typeof(___table)* _entry()                        \
{                                                 \
  func_vtable_init(&___table);                    \
  ___table.init = func_init;                      \
  return &___table;                               \
}

#include "module_debug.h"
#include "module_types.h"
#include "module_vtables.h"

#endif
