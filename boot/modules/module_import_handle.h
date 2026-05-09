#ifndef MODULE_IMPORT_HANDLE_H
#define MODULE_IMPORT_HANDLE_H

#include <efi.h>
#include <efilib.h>

typedef struct MODULE_IMPORT_HANDLE
{
    CONST CHAR8 *TypeString;
    CONST CHAR8 *NameString;
    CONST CHAR8 *ParentTypeString;
    CONST CHAR8 *ParentNameString;
    VOID *VTablePtr;
} MODULE_IMPORT_HANDLE;

VOID
AddModuleImportHandle(EFI_SYSTEM_TABLE *SystemTable, MODULE_IMPORT_HANDLE ModuleImportHandle);

EFI_STATUS
HandleModuleImports();

EFI_STATUS
ModuleImportHandleInit(EFI_SYSTEM_TABLE *SystemTable);

#endif
