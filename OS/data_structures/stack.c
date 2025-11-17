#include "stack.h"

#define STACK_INIT_IMPL(stack_size)                                                \
stack##stack_size##_t stack##stack_size##_init(uint##stack_size##_t *data, size_t size)   \
{                                                                                   \
  stack##stack_size##_t stack;                                                            \
  stack.capacity = size;                                                            \
  stack.top = 0;                                                                    \
  stack.data = data;                                                                \
  return stack;                                                                     \
}

#define STACK_POP_IMPL(stack_size)                                        \
uint##stack_size##_t stack##stack_size##_pop(stack##stack_size##_t* stack)  \
{                                                                           \
  return stack->data[--stack->top];                                         \
}

#define STACK_PUSH_IMPL(stack_size)                                                 \
void stack##stack_size##_push(stack##stack_size##_t *stack, uint##stack_size##_t val) \
{                                                                                     \
  stack->data[stack->top++] = val;                                                    \
}

STACK_INIT_IMPL(8)
STACK_INIT_IMPL(16)
STACK_INIT_IMPL(32)
STACK_INIT_IMPL(64)

STACK_PUSH_IMPL(8)
STACK_PUSH_IMPL(16)
STACK_PUSH_IMPL(32)
STACK_PUSH_IMPL(64)

STACK_POP_IMPL(8)
STACK_POP_IMPL(16)
STACK_POP_IMPL(32)
STACK_POP_IMPL(64)
