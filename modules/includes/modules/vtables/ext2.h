#ifndef EXT2_VTABLE_H
#define EXT2_VTABLE_H

#include "../../module_vtable.h"
#include "../structures/ext2.h"

typedef struct ext2_vtable_t 
{
  vtable_def
  void* (*create_fs)(unsigned int, unsigned int);
} ext2_vtable_t;

#endif