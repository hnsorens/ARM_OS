#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <efi.h>
#include "serial.h"

EFI_STATUS
OpenRoot(
        IN EFI_SYSTEM_TABLE *SystemTable,
        IN EFI_HANDLE ImageHandle,
        OUT EFI_FILE_PROTOCOL **Root
);

EFI_STATUS
ReadFile(
        IN CHAR16 *FileName,
        IN EFI_FILE_PROTOCOL *Root,
        IN EFI_SYSTEM_TABLE *SystemTable,
        IN EFI_HANDLE ImageHandle,
        OUT CHAR8 **Buffer
);



#endif
