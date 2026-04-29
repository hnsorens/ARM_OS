#include <efi.h>
#include <efilib.h>

#include "boot_services.h"

EFI_STATUS
EFIAPI
efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);

    Print(L"Hello World\n");


    MEMORY_MAP MemoryMap;
    UINTN MemoryMapRegionsCount;
    ExitBootServices(ImageHandle, SystemTable, &MemoryMap, &MemoryMapRegionsCount);

    while(1) { __asm__ volatile("wfi"); }
    return EFI_SUCCESS;
}
