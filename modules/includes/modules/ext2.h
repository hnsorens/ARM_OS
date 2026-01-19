#ifndef EXT2_H
#define EXT2_H

#include "modules/structures/ext2.h"
#include "module_vtables.h"

#ifndef EXT2
#define EXT2 ext2
#endif

#define EXPAND(var) var
#define CONCAT(a, b) a##b
#define CONCAT_EXPAND(a, b) CONCAT(a, b)

#define GLOBAL __attribute__((visibility("hidden")))

#ifdef __MAIN__

#define __EXT2__DEF(prefix) \
GLOBAL void* (*CONCAT_EXPAND(prefix, _create_fs))( unsigned int, unsigned int ) = 0; \
\
static void ext2_fetch(kernel_vtable_t *kvtable){\
	ext2_vtable_t* module = (ext2_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_EXT2);\
	CONCAT_EXPAND(prefix, _create_fs) = module->create_fs;\
}

__EXT2__DEF(EXT2) 
#undef __EXT2__DEF

#else

#define __EXT2__DEF(prefix) \
GLOBAL extern void* (*CONCAT_EXPAND(prefix, _create_fs))( unsigned int, unsigned int ); \


__EXT2__DEF(EXT2) 
#undef GLOBAL
#undef __EXT2__DEF
#undef EXT2

#endif
#endif