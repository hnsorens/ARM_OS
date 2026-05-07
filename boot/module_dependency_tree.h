#ifndef MODULE_DEPENDENCY_TREE_H
#define MODULE_DEPENDENCY_TREE_H

#include <efi.h>
#include <efilib.h>

typedef struct MODULE_DEPENDENCY
{
    UINTN ModuleDependencyCount;
    struct MODULE_DEPENDENCY **Dependencies;
    VOID (*Init)(VOID *);
    BOOLEAN Initialized;
} MODULE_DEPENDENCY;

#endif
