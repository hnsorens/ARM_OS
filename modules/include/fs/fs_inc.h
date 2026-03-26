#ifndef __FS_INC_H__
#define __FS_INC_H__


#include "fs_types.h"

#ifndef CONCAT_HIDDEN
#define CONCAT_HIDDEN(a, b) a ## b
#define CONCAT(a, b) CONCAT_HIDDEN(a, b)
#endif

#define fs_create_fs CONCAT(FS_NAME, _create_fs_func)

extern void* fs_create_fs( unsigned int, unsigned int );
#ifdef FS_SYMLINK_EXTENSION
extern void fs_set_symlink( void );
#endif
#endif