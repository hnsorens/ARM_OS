#ifndef EXT2_H
#define EXT2_H
#include "module_vtables.h"

#ifndef EXT2
#define EXT2 ext2
#endif

#define EXPAND(var) var
#define CONCAT(a, b) a##b
#define CONCAT_EXPAND(a, b) CONCAT(a, b)

#ifdef __MAIN__

#define __EXT2__DEF(prefix) \
__attribute__((visibility("hidden"))) void* (*CONCAT_EXPAND(prefix, _create_fs))( unsigned int, unsigned int ) = 0; \
\
static void _ext2_init(kernel_vtable_t *kvtable){\
	ext2_vtable_t* module = (ext2_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_EXT2);\
	CONCAT_EXPAND(prefix, _create_fs) = module->create_fs;\
}

__EXT2__DEF(EXT2) 
#undef __EXT2__DEF

#else

#define __EXT2__DEF(prefix) \
__attribute__((visibility("hidden"))) extern void* (*CONCAT_EXPAND(prefix, _create_fs))( unsigned int, unsigned int ); \


__EXT2__DEF(EXT2) 
#undef __EXT2__DEF

#endif
#endif