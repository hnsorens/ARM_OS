
#include "Guid/FileInfo.h"
#include "ProcessorBind.h"
#include "Protocol/SimpleFileSystem.h"
#include "../../modules/include/module_enum.h"
#include "../../modules/include/module_names.h"

#define MODULE_NAMES module_names


EFI_STATUS
LoadKernel(
  IN   EFI_SYSTEM_TABLE *SystemTable,
  OUT  VOID **Kernel
);
