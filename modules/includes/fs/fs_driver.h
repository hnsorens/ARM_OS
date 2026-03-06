#ifndef __FS_DRIVER_H__
#define __FS_DRIVER_H__

#include "fs.h"
#include "../module_types.h"
#include "fs_types.h"

typedef struct fs_driver {
	void (*fetch)(core_ops*);
	void (*start)(core_ops*);
	fs_ops* fs;
	fs_symlink_ext* fs_symlink_ext;
} fs_driver;

#endif