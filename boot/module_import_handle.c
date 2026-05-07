#include "module_import_handle.h"

#include "module_registry.h"
#include "serial.h"

#define MODULE_IMPORT_HANDLE_INITIAL_SIZE 32

MODULE_IMPORT_HANDLE *ModuleImportHandleArray = 0;
UINTN ModuleImportHandleSize = MODULE_IMPORT_HANDLE_INITIAL_SIZE;
UINTN ModuleImportHandleCapacity = 0;

static VOID *Memcpy(VOID *Dest, CONST VOID *Src, UINTN N)
{
	UINT8 *D = Dest;
	CONST UINT8 *S = Src;
	while (N--)
		*D++ = *S++;
	return Dest;
}

VOID ModuleImportHandleInit(EFI_SYSTEM_TABLE *SystemTable)
{
	EFI_STATUS Status = SystemTable->BootServices->AllocatePool(
		EfiLoaderData,
		sizeof(MODULE_IMPORT_HANDLE) *
			MODULE_IMPORT_HANDLE_INITIAL_SIZE,
		(VOID *)&ModuleImportHandleArray);
}

VOID AddModuleImportHandle(EFI_SYSTEM_TABLE *SystemTable,
			   MODULE_IMPORT_HANDLE ModuleImportHandle)
{
	if (ModuleImportHandleCapacity == ModuleImportHandleSize) {
		MODULE_IMPORT_HANDLE *NewArray = 0;
		EFI_STATUS Status = SystemTable->BootServices->AllocatePool(
			EfiLoaderData,
			sizeof(MODULE_IMPORT_HANDLE) * ModuleImportHandleSize *
				2,
			(VOID *)&NewArray);
		Memcpy(NewArray, ModuleImportHandleArray,
		       sizeof(MODULE_IMPORT_HANDLE) * ModuleImportHandleSize);
		Status = SystemTable->BootServices->FreePool(
			ModuleImportHandleArray);
		ModuleImportHandleArray = NewArray;
		ModuleImportHandleSize *= 2;
	}

	Memcpy(&ModuleImportHandleArray[ModuleImportHandleCapacity],
	       &ModuleImportHandle, sizeof(MODULE_IMPORT_HANDLE));
	ModuleImportHandleCapacity++;
}

VOID HandleModuleImports()
{
	for (UINTN I = 0; I < ModuleImportHandleCapacity; ++I) {
		Boot_Log("HEHE HAHA\n", 10);
		Boot_Log(ModuleImportHandleArray[I].NameString, 5);
		Boot_Log_Hex((UINT64)(ModuleImportHandleArray[I].NameString));
		Boot_Log("\n", 1);
		Boot_Log(ModuleImportHandleArray[I].TypeString, 5);
		Boot_Log_Hex((UINT64)(ModuleImportHandleArray[I].TypeString));
		Boot_Log("\n", 1);
		VOID *VTableSource = 0;
		UINTN VTableSize = 0;
		if (!ModuleImportHandleArray[I].NameString) {
			RegistryGetAny(ModuleImportHandleArray[I].TypeString,
					 &VTableSource, &VTableSize);
		} else {
            RegistryGet(ModuleImportHandleArray[I].TypeString,
				     ModuleImportHandleArray[I].NameString,
				     &VTableSource, &VTableSize);
		}

		Boot_Log_Hex((UINT64)ModuleImportHandleArray[I].VTablePtr);
		Boot_Log("\n", 1);
		Boot_Log_Hex((UINT64)VTableSource);
		Boot_Log("\n", 1);

		Memcpy(ModuleImportHandleArray[I].VTablePtr, VTableSource,
		       VTableSize); // Change the size so it is gud
		//
		Boot_Log_Hex(*(UINT64 *)ModuleImportHandleArray[I].VTablePtr);
		Boot_Log("\n", 1);
		Boot_Log_Hex(*(UINT64 *)VTableSource);
		Boot_Log("\n", 1);
	}
}
