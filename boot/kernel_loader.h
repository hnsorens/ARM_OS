#ifndef KERNEL_LOADER_H
#define KERNEL_LOADER_H

#include <efi.h>
#include <efilib.h>
#include "page_table.h"

EFI_STATUS
Load_Kernel(EFI_SYSTEM_TABLE *SystemTable, EFI_HANDLE ImageHandle, EFI_FILE_PROTOCOL *Root, CHAR16 *ConfigurationFileName, EFI_VIRTUAL_ADDRESS *Entry, PAGE_TABLE_T *UpperPageTable);

#endif
