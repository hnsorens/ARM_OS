#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include <efi.h>
#include <efilib.h>
#include "page_table.h"

EFI_STATUS
Load_Kernel(
        IN EFI_SYSTEM_TABLE *SystemTable,
        IN CHAR8 *KernelElfBuffer,
        OUT PAGE_TABLE_T *UpperPageTable
);

#endif
