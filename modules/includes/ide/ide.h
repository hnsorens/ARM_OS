#ifndef IDE_VTABLE_H
#define IDE_VTABLE_H


#include "ide_types.h"

typedef struct ide_ops
{
  void* (*read_fn)(unsigned int, unsigned int);
  void (*write_fn)(unsigned int, unsigned int, void*);
} ide_ops;

#endif