#include "module_import_handle.h"

#include "module_registry.h"

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
	       &ModuleImportHandleArray, sizeof(MODULE_IMPORT_HANDLE));
	ModuleImportHandleCapacity++;
}

VOID HandleModuleImports()
{
	for (UINTN I = 0; I < ModuleImportHandleCapacity; ++I) {
		VOID *VTableSource = 0;
		if (!ModuleImportHandleArray[I].NameString) {
			VTableSource = registry_get_any(
				ModuleImportHandleArray[I].TypeString);
		} else {
			VTableSource = registry_get(
				ModuleImportHandleArray[I].TypeString,
				ModuleImportHandleArray[I].NameString);
		}

		Memcpy(ModuleImportHandleArray[I].VTablePtr, VTableSource,
		       8); // Change the size so it is gud
	}
}
