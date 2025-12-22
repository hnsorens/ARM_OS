#ifndef MODULE_H
#define MODULE_H

#define vtable_def void (*init)(kernel_vtable_t*, virt_addr_t load);
#define vtable(type) struct type ___table;
#define start(func_vtable_init, func_init)        \
void func_vtable_init(typeof(___table)* table);   \
void func_init(kernel_vtable_t* vtable, virt_addr_t load);          \
__attribute__((section(".text._entry")))          \
typeof(___table)* _entry()                        \
{                                                 \
  func_vtable_init(&___table);                    \
  ___table.init = func_init;                      \
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

#endif
