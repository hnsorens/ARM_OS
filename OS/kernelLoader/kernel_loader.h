#ifndef OS_LOADER_H
#define OS_LOADER_H

#include <Uefi.h>


EFI_STATUS
LoadKernel(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE* SystemTable);

#endif
