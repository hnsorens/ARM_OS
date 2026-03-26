#ifndef __FS_INC_H__
#define __FS_INC_H__


#include "fs_types.h"

#ifndef CONCAT_HIDDEN
#define CONCAT_HIDDEN(a, b) a ## b
#define CONCAT(a, b) CONCAT_HIDDEN(a, b)
#endif

#define fs_create_fs CONCAT(IMPL_NAME, _create_fs_func)

__attribute__((used)) void* fs_create_fs( unsigned int, unsigned int );
#ifdef FS_SYMLINK_EXTENSION
__attribute__((used)) void fs_set_symlink( void );
#endif
typedef void (*init_fn_t)(void);
#define __init_func __attribute__((section(".init_array"), used))
#define MODULE_INIT(func) \
static init_fn_t __init_ptr##func __init_func = func;
#endif