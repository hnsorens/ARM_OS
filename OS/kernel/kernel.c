#include "Library/UefiBootServicesTableLib.h"
#include <Uefi.h>
#include <stdint.h>

EFI_STATUS EFIAPI _ModuleEntryPoint(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable) {
    gImageHandle = ImageHandle;
    gST = SystemTable;
    gBS = SystemTable->BootServices;

    
    
    return EFI_SUCCESS;
}
