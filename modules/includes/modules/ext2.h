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

#ifdef __MAIN__
#define GLOBAL __attribute__((visibility("hidden")))
#define END = 0;
#else
#define GLOBAL __attribute__((visibility("hidden"))) extern 
#define END ;
#endif

GLOBAL void* (*CONCAT_EXPAND(EXT2, _create_fs))( unsigned int, unsigned int ) END 
#ifdef __MAIN__

static void ext2_fetch(kernel_vtable_t *kvtable){
	ext2_vtable_t* module = (ext2_vtable_t*)kvtable->find_module_vtable_by_type(MODULE_EXT2);
	CONCAT_EXPAND(EXT2, _create_fs) = module->create_fs;
}
#endif

#undef GLOBAL
#undef EXT2

#endif