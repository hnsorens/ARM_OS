#ifndef HEAP_H
#define HEAP_H

#include "module_vtables.h"

typedef struct block_header_t
{
  unsigned long size;
  struct block_header_t* next;
  int is_free;
} block_header_t;

typedef struct heap_t
{
  unsigned long start;
  unsigned long end;
  unsigned long total_size;
  block_header_t* free_list;
} heap_t;

void heap_init(heap_t* heap, unsigned long heap_base, unsigned long heap_size);
unsigned long heap_get_used_memory(heap_t* heap);
unsigned long heap_get_free_memory(heap_t* heap);
unsigned long heap_get_total_memory(heap_t* heap);
unsigned long heap_malloc(heap_t* heap, unsigned long size);
unsigned long heap_calloc(heap_t* heap, unsigned long num, unsigned long size);
unsigned long heap_realloc(heap_t* heap, unsigned long ptr, unsigned long new_size);
void heap_free(heap_t* heap, unsigned long ptr);

#endif