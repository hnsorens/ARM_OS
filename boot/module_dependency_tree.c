#include "module_dependency_tree.h"
#include "memory_constants.h"
#include "serial.h"

#define INITIAL_DEPENDENCY_COUNT 32
#define MAX_DEPENDENCY_DEPTH 20

MODULE_DEPENDENCY* DependencyNodes = 0;
UINTN DependencySize = 0;
UINTN DependencyCapacity = INITIAL_DEPENDENCY_COUNT;

static VOID *Memcpy(VOID *Dest, CONST VOID *Src, UINTN N)
{
	UINT8 *D = Dest;
	CONST UINT8 *S = Src;
	while (N--)
		*D++ = *S++;
	return Dest;
}

EFI_STATUS InitializeModuleDependencyGraph(EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS Status;
    Status = SystemTable->BootServices->AllocatePool(FREE_AFTER_BOOT, sizeof(MODULE_DEPENDENCY) * INITIAL_DEPENDENCY_COUNT, (VOID*)&DependencyNodes);
    if (EFI_ERROR(Status))
    {
        Boot_Log("Failed to allocate module dependency graph\n", 40);
        return Status;
    }
    Boot_Log("Allocated module dependency graph\n", 34);
    return EFI_SUCCESS;
}

EFI_STATUS AddModuleDependency(EFI_SYSTEM_TABLE *SystemTable, MODULE_DEPENDENCY Dependency)
{
    EFI_STATUS Status;
    if (DependencyCapacity == DependencySize)
    {
        MODULE_DEPENDENCY *NewArray;
        Status = SystemTable->BootServices->AllocatePool(FREE_AFTER_BOOT, sizeof(MODULE_DEPENDENCY) * DependencyCapacity * 2, (VOID *)NewArray);
        if (EFI_ERROR(Status))
        {
            Boot_Log("Failed to reallocated module dependency graph\n", 46);
            return Status;
        }
        Boot_Log("Reallocated module dependency graph\n", 36);
        Memcpy(NewArray, DependencyNodes, sizeof(MODULE_DEPENDENCY) * DependencyCapacity);
        DependencyNodes = NewArray;
        DependencyCapacity *= 2;
    }

    Memcpy(&DependencyNodes[DependencySize], &Dependency, sizeof(MODULE_DEPENDENCY));
    ++DependencySize;
    return EFI_SUCCESS;
}

static EFI_STATUS InitModule(MODULE_DEPENDENCY *Dependency, UINTN Depth)
{
    if (Depth >= MAX_DEPENDENCY_DEPTH) return EFI_INVALID_PARAMETER;
    for (UINTN I = 0; I < Dependency->ModuleDependencyCount; ++I)
    {
        InitModule(Dependency->Dependencies[I], Depth + 1);
    }
    if (!Dependency->Initialized)
    {
        Dependency->Initialized = TRUE;
        Dependency->Init(0);
    }
    return EFI_SUCCESS;
}

EFI_STATUS InitModules()
{
    EFI_STATUS Status;
    for (UINTN I = 0; I < DependencySize; ++I)
    {
        MODULE_DEPENDENCY *Dependency = &DependencyNodes[I];
        if (Dependency->Initialized) continue;
        Status = InitModule(Dependency, 0);
        if (EFI_ERROR(Status))
        {
            Boot_Log("Module dependency loop detected\n", 32);
            return Status;
        }

        if (!Dependency->Initialized)
        {
            Dependency->Initialized = TRUE;
            Dependency->Init(0);
        }
    }

    Boot_Log("Initialized modules\n", 20);

    return EFI_SUCCESS;
}


