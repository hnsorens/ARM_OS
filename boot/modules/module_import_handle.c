#include "module_import_handle.h"

#include "efidef.h"
#include "memory/memory_constants.h"
#include "modules/module_registry.h"
#include "logging/serial.h"

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

EFI_STATUS ModuleImportHandleInit(EFI_SYSTEM_TABLE *SystemTable)
{
	EFI_STATUS Status = SystemTable->BootServices->AllocatePool(
		FREE_AFTER_BOOT,
		sizeof(MODULE_IMPORT_HANDLE) *
			MODULE_IMPORT_HANDLE_INITIAL_SIZE,
		(VOID *)&ModuleImportHandleArray);
	if (EFI_ERROR(Status)) {
		Fail_Log("Allocated import handle buffer\n", 32);
		return Status;
	}
	Ok_Log("Allocated import handle buffer\n", 32);
	return Status;
}

VOID AddModuleImportHandle(EFI_SYSTEM_TABLE *SystemTable,
			   MODULE_IMPORT_HANDLE ModuleImportHandle)
{
	if (ModuleImportHandleCapacity == ModuleImportHandleSize) {
		MODULE_IMPORT_HANDLE *NewArray = 0;
		EFI_STATUS Status = SystemTable->BootServices->AllocatePool(
			FREE_AFTER_BOOT,
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

EFI_STATUS HandleModuleImports()
{
	EFI_STATUS Status;
	for (UINTN I = 0; I < ModuleImportHandleCapacity; ++I) {
		VOID *VTableSource = 0;
		UINTN VTableSize = 0;
		MODULE_IMPORT_HANDLE *ImportHandle =
			&ModuleImportHandleArray[I];

		if (!ImportHandle->NameString) {
			RegistryResolveName(
				ModuleImportHandleArray[I].TypeString,
				&ModuleImportHandleArray[I].NameString);
		}

		Status = RegistryPutDependency(ImportHandle->ParentTypeString,
					       ImportHandle->ParentNameString,
					       ImportHandle->TypeString,
					       ImportHandle->NameString);
		if (EFI_ERROR(Status)) {
			Fail_Log("Put dependency in registry\n", 27);
			return Status;
		}
		Ok_Log("Put dependency in registry\n", 27);

		RegistryGet(ModuleImportHandleArray[I].TypeString,
			    ModuleImportHandleArray[I].NameString,
			    &VTableSource, &VTableSize);

		Memcpy(ModuleImportHandleArray[I].VTablePtr, VTableSource,
		       VTableSize); // Change the size so it is gud
	}
	return EFI_SUCCESS;
}
