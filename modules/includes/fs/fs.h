#ifndef EXT2_VTABLE_H
#define EXT2_VTABLE_H


#include "fs_types.h"

typedef struct fs_symlink_ext
{
  void (*set_symlink)(void);
} fs_symlink_ext;

typedef struct fs_ops
{
  void* (*create_fs)(unsigned int, unsigned int);
} fs_ops;

#endif