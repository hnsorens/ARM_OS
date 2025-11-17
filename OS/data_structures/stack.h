#ifndef STACK_H
#define STACK_H



#include <stdint.h>
#include <stddef.h>

#define STACK_INFO size_t capacity; size_t top;

#define STACK_STRUCT(size)     \
typedef struct stack##size##_t \
{                              \
  size_t capacity;             \
  size_t top;                  \
  uint##size##_t *data;        \
} stack##size##_t;

STACK_STRUCT(8)
STACK_STRUCT(16)
STACK_STRUCT(32)
STACK_STRUCT(64)



#define STACK_INIT_DEFINE(stack_size) stack##stack_size##_t stack##stack_size##_init(uint##stack_size##_t *data, size_t size);
#define STACK_POP_DEFINE(stack_size) uint##stack_size##_t stack##stack_size##_pop(stack##stack_size##_t* stack);
#define STACK_PUSH_DEFINE(stack_size) void stack##stack_size##_push(stack##stack_size##_t *stack, uint##stack_size##_t val);

STACK_INIT_DEFINE(8)
STACK_INIT_DEFINE(16)
STACK_INIT_DEFINE(32)
STACK_INIT_DEFINE(64)

STACK_PUSH_DEFINE(8)
STACK_PUSH_DEFINE(16)
STACK_PUSH_DEFINE(32)
STACK_PUSH_DEFINE(64)

STACK_POP_DEFINE(8)
STACK_POP_DEFINE(16)
STACK_POP_DEFINE(32)
STACK_POP_DEFINE(64)

#endif
