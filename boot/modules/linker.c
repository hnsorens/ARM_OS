#include "linker.h"

static UINT32 Strcmp(CONST CHAR8 *S1, CONST CHAR8 *S2)
{
	while (*S1 && (*S1 == *S2)) {
		S1++;
		S2++;
	}
	return *(UINT8 *)S1 - *(UINT8 *)S2;
}

EFI_STATUS
Link_Elf_Module(UINT64 Delta, VOID *ElfBuffer)
{
	Elf64_Ehdr *Ehdr = (Elf64_Ehdr *)ElfBuffer;

	// Shift the Entry Point (The "Start" address)
	if (Ehdr->e_entry != 0)
		Ehdr->e_entry += Delta;

	// Shift Program Headers (The "Load" addresses)
	// This tells the OS/Loader where the segments live in VM
	Elf64_Phdr *Phdrs =
		(Elf64_Phdr *)((uint8_t *)ElfBuffer + Ehdr->e_phoff);
	for (INT32 I = 0; I < Ehdr->e_phnum; ++I) {
		if (Phdrs[I].p_vaddr != 0) {
			Phdrs[I].p_vaddr += Delta;
			Phdrs[I].p_paddr += Delta;
		}
	}

	// Shift Section Headers (The metadata labels)
	Elf64_Shdr *Shdrs =
		(Elf64_Shdr *)((uint8_t *)ElfBuffer + Ehdr->e_shoff);
	for (INT32 I = 0; I < Ehdr->e_shnum; ++I) {
		if (Shdrs[I].sh_addr != 0) {
			Shdrs[I].sh_addr += Delta;
		}
	}

	// Shift Symbol Table (Function/Variable pointers)
	for (INT32 I = 0; I < Ehdr->e_shnum; ++I) {
		if (Shdrs[I].sh_type == SHT_SYMTAB ||
		    Shdrs[I].sh_type == SHT_DYNSYM) {
			Elf64_Sym *Syms = (Elf64_Sym *)((uint8_t *)ElfBuffer +
							Shdrs[I].sh_offset);
			INT32 Count = Shdrs[I].sh_size / sizeof(Elf64_Sym);
			for (INT32 J = 0; J < Count; ++J) {
				if (Syms[J].st_shndx != SHN_UNDEF) {
					Syms[J].st_value += Delta;
				}
			}
		}
	}

	// We look for relocation tables (RELA) and apply the shift to the memory
	// locations they point to. This shifts R_AARCH64_ABS64.
	for (int i = 0; i < Ehdr->e_shnum; i++) {
		if (Shdrs[i].sh_type == SHT_RELA) {
			Elf64_Rela *Relas =
				(Elf64_Rela *)((uint8_t *)ElfBuffer +
					       Shdrs[i].sh_offset);
			INT32 Count = Shdrs[i].sh_size / sizeof(Elf64_Rela);

			// The 'sh_info' of a RELA section holds the index of the section
			// being patched (e.g., .data or .text)
			Elf64_Shdr *TargetSection = &Shdrs[Shdrs[i].sh_info];
			UINT8 *TargetBuffer =
				(UINT8 *)ElfBuffer + TargetSection->sh_offset;

			for (int j = 0; j < Count; j++) {
				UINT64 Type = ELF64_R_TYPE(Relas[j].r_info);

				// If it's an absolute 64-bit address relocation
				// R_AARCH64_ABS64 (257)
				if (Type == R_AARCH64_ABS64) {
					// Find where the pointer is located inside the section
					// We use the original sh_addr (before we shifted it) to find the offset
					UINT64 OffsetInSection =
						Relas[j].r_offset -
						(TargetSection->sh_addr -
						 Delta);

					// Update the actual 8 bytes in the buffer!
					UINT64 *PtrToPatch =
						(UINT64 *)(TargetBuffer +
							   OffsetInSection);
					*PtrToPatch += Delta;
				}
			}
		}
	}

	return EFI_SUCCESS;
}
