#include <Uefi.h>

#include "kernelLoader/kernel_loader.h"

EFI_STATUS EFIAPI _ModuleEntryPoint(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable) {

  SystemTable->ConOut->OutputString(SystemTable->ConOut, u"Hellow World");

  return EFI_SUCCESS;
}

