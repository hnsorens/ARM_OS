#ifndef LINKER_H
#define LINKER_H

#include <efi.h>
#include <efilib.h>
#include "elf.h"

EFI_STATUS
Link_Elf_Module(UINT64 delta, void *buffer);

#endif
