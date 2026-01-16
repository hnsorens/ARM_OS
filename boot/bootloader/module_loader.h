
#include "Guid/FileInfo.h"
#include "ProcessorBind.h"
#include "Protocol/SimpleFileSystem.h"
#include "../../modules/includes/module_enum.h"
#include "../../modules/includes/module_names.h"

#define MODULE_NAMES module_names

typedef module_type_t MODULE_TYPE;

typedef struct MODULE_LOAD {
  CHAR16 *ModulePath;
  CHAR16 *ModuleName;
  MODULE_TYPE Type;
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
  MODULE_TYPE Type;
} MODULE;

typedef struct MODULE_TABLE
{
  UINTN ModuleCount;
  MODULE *Modules;
} MODULE_TABLE;


EFI_STATUS
LoadModules(EFI_SYSTEM_TABLE *SystemTable, MODULE_TABLE *ModuleTable);
