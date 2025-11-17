#ifndef UEFI_HELPERS_H
#define UEFI_HELPERS_H

#include "ProcessorBind.h"
#include "Uefi/UefiBaseType.h"
#include "Uefi/UefiSpec.h"

void 
PrintNumber(EFI_SYSTEM_TABLE *SystemTable, UINT64 number);

#endif
