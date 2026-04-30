#include <efi.h>
#include <efilib.h>

#include "boot_services.h"
#include "filesystem.h"

EFI_STATUS
EFIAPI
efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {

    EFI_STATUS Status;

    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Hello World\n");

    EFI_FILE_PROTOCOL* Root = NULL;
    Status = OpenRoot(SystemTable, ImageHandle, &Root);
    if (EFI_ERROR(Status))
    {
        return Status;
    }
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Opened Root\n");

    CHAR8* Buffer;
    ReadFile(L"\\kernel.bin", Root, SystemTable, ImageHandle, &Buffer);


    MEMORY_MAP MemoryMap;
    UINTN MemoryMapRegionsCount;
    ExitBootServices(ImageHandle, SystemTable, &MemoryMap, &MemoryMapRegionsCount);

    while(1) { __asm__ volatile("wfi"); }
    return EFI_SUCCESS;
}
