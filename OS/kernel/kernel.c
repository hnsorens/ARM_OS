#include <Uefi.h>

EFI_STATUS EFIAPI _ModuleEntryPoint(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable) {

  SystemTable->ConOut->OutputString(SystemTable->ConOut, u"Hellow World!");
  while (1);
  return EFI_SUCCESS;
}

