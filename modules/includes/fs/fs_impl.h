#ifndef __FS_IMPL_T__
#define __FS_IMPL_T__

#include "fs_driver.h"
#include "fs.h"

#define __MODULE_NAME__ FS
#define __MODULE_NAME_STR__ "FS"
#define __MAIN__

fs_ops __fs__;
fs_driver __fs_ops__;
#ifdef FS_SYMLINK_EXTENSION
fs_symlink_ext __fs_symlink_ext__;
#endif

unsigned long __load_offset__;

void fs_init(fs_ops* fs);
void fs_fetch(core_ops* ops);
void fs_start(core_ops* ops);
void fs_symlink_ext_init(fs_symlink_ext* fs_symlink);

__attribute__((section(".text._entry")))
fs_driver *_entry(unsigned long offset) {
	__load_offset__ = offset;

	fs_init(&__fs__);
	__fs_ops__.fs = &__fs__;

	#ifdef FS_SYMLINK_EXTENSION
	fs_symlink_ext_init(&__fs_symlink_ext__);
	__fs_ops__.fs_symlink_ext = &__fs_symlink_ext__;
	#else
	__fs_ops__.fs_symlink_ext = 0;
	#endif

	__fs_ops__.start = fs_start;
	__fs_ops__.fetch = fs_fetch;

	return &__fs_ops__;
}

#endif