
#include "Guid/FileInfo.h"
#include "ProcessorBind.h"
#include "Protocol/SimpleFileSystem.h"

typedef struct MODULE_LOAD {
  CHAR16 *ModulePath;
  CHAR16 *ModuleName;
  EFI_FILE_INFO *Info;
  EFI_FILE_PROTOCOL *File;
  UINTN ModuleBase;
  UINTN VTable;
} MODULE_LOAD;

typedef struct MODULE
{
  char ModuleName[32];
  UINTN Size;
  UINTN ModuleBase;
  UINTN VTable;
} MODULE;

typedef struct MODULE_TABLE
{
  UINTN ModuleCount;
  MODULE *Modules;
} MODULE_TABLE;


EFI_STATUS
LoadModules(EFI_SYSTEM_TABLE *SystemTable, MODULE_TABLE *ModuleTable);
