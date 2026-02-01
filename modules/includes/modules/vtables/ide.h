#ifndef IDE_VTABLE_H
#define IDE_VTABLE_H

#include "../../module_vtable.h"
#include "../structures/ide.h"

typedef struct ide_vtable_t 
{
  vtable_def
  void* (*read_fn)(unsigned int, unsigned int);
  void (*write_fn)(unsigned int, unsigned int, void*);
} ide_vtable_t;

#endif