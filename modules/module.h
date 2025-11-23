#ifndef MODULE_H
#define MODULE_H


#define vtable(type) struct type ___table;
#define start(function) \
void function(typeof(___table) *table);   \
__attribute__((section(".text._entry")))  \
typeof(___table)* _entry()                \
{                                         \
  function(&___table);                    \
  return &___table;                       \
}

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


#endif
