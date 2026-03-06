#ifndef __FS_INC_H__
#define __FS_INC_H__


#include "fs_driver.h"

#ifdef __MAIN__
#define GLOBAL __attribute__((visibility("hidden")))
#define END = 0;
#else
#define GLOBAL __attribute__((visibility("hidden"))) extern
#define END ;
#endif

GLOBAL void* (*fs_create_fs)( unsigned int, unsigned int ) END 
#ifdef FS_SYMLINK_EXTENSION
GLOBAL void (*fs_set_symlink)( void ) END 
#endif

#ifdef __MAIN__

static void fs_fetch(core_ops *ops) {
	fs_driver *driver = (fs_driver*)ops->find_module_by_type(MODULE_FS);
fs_create_fs = driver->fs->create_fs;
#ifdef FS_SYMLINK_EXTENSION
fs_symlink_set_symlink = ops->fs_symlink_ext->set_symlink;
#endif
}

#endif
#endif