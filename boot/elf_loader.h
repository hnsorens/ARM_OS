#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include <efi.h>
#include <efilib.h>

#include "elf.h"
#include "page_table.h"

EFI_STATUS
Load_Elf(IN EFI_SYSTEM_TABLE *SystemTable, IN CHAR8 *ElfBuffer, IN UINT64 LoadOffset,
	    OUT PAGE_TABLE_T *UpperPageTable, OUT EFI_VIRTUAL_ADDRESS *Entry);

#endif
