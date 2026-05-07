#include "elf_loader.h"

#include "linker.h"
#include "module_registry.h"
#include "serial.h"
#include "module_import_handle.h"
#include "elf.h"
#include "memory_constants.h"

#define PAGE_SIZE 4096

static INT32 Strncmp(CONST CHAR8 *S1, CONST CHAR8 *S2, UINTN N)
{
	while (N--) {
		if (*S1 != *S2)
			return *(UINT8 *)S1 - *(UINT8 *)S2;
		if (*S1 == 0)
			break;
		S1++;
		S2++;
	}
	return 0;
}

static CHAR8 *StrChr(CONST CHAR8 *S, UINT8 C)
{
	while (*S != C) {
		if (!*S)
			return 0;
		S++;
	}
	return (CHAR8 *)S;
}

static VOID *Memcpy(VOID *Dest, CONST VOID *Src, UINTN N)
{
	UINT8 *D = Dest;
	CONST UINT8 *S = Src;
	while (N--)
		*D++ = *S++;
	return Dest;
}

static BOOLEAN Verify_Elf(Elf64_Ehdr *Header)
{
	return Header->e_ident[0] == 0x7F && Header->e_ident[1] == 'E' &&
	       Header->e_ident[2] == 'L' && Header->e_ident[3] == 'F';
}

static INT32 Strlen(CONST CHAR8 *S)
{
	UINT32 I = 0;
	while (S[I])
		I++;
	return I;
}

static UINTN GetElfSpanPages(void *ElfFileBuffer)
{
	Elf64_Ehdr *Header = (Elf64_Ehdr *)ElfFileBuffer;

	// Basic ELF Magic Check
	if (Header->e_ident[0] != 0x7f || Header->e_ident[1] != 'E') {
		return 0;
	}

	UINT64 MinAddr = (UINT64)-1;
	UINT64 MaxAddr = 0;
	INT32 Found = 0;

	Elf64_Phdr *PhdrTable =
		(Elf64_Phdr *)((uint8_t *)ElfFileBuffer + Header->e_phoff);

	for (UINT32 I = 0; I < Header->e_phnum; I++) {
		// PT_LOAD segment check
		if (PhdrTable[I].p_type == PT_LOAD) {
			UINT64 Start = PhdrTable[I].p_vaddr;
			UINT64 End = Start + PhdrTable[I].p_memsz;

			if (Start < MinAddr)
				MinAddr = Start;
			if (End > MaxAddr)
				MaxAddr = End;
			Found = 1;
		}
	}

	if (!Found)
		return 0;

	// Manual Page Alignment and Span Calculation
	// Page Size is 4096 (0x1000)
	UINT64 SpanStart = MinAddr & ~((UINT64)4095);
	UINT64 SpanEnd = (MaxAddr + 4095) & ~((UINT64)4095);

	return (UINTN)((SpanEnd - SpanStart) / 4096);
}

UINTN LoadOffset = 0;

EFI_STATUS
Load_Elf(IN EFI_SYSTEM_TABLE *SystemTable, IN CHAR8 *ElfBuffer,
	 OUT PAGE_TABLE_T *UpperPageTable, OUT EFI_VIRTUAL_ADDRESS *Entry)
{
	if (!ElfBuffer)
		return EFI_LOAD_ERROR;

	EFI_STATUS Status;

	Elf64_Ehdr *Ehdr = (Elf64_Ehdr *)ElfBuffer;

	// Verify that Kernel is an Elf File
	if (!Verify_Elf(Ehdr)) {
		SystemTable->ConOut->OutputString(
			SystemTable->ConOut,
			L"[Boot] Kernel Not a Valid Elf!\n");
		return EFI_LOAD_ERROR;
	}

	Link_Elf_Module(LoadOffset + 0xFFFF800000000000, ElfBuffer);
	LoadOffset += GetElfSpanPages(ElfBuffer) * 4096;

	Elf64_Phdr *Phdr = (Elf64_Phdr *)((UINT8 *)Ehdr + Ehdr->e_phoff);
	Elf64_Shdr *Shdr = (Elf64_Shdr *)((UINT8 *)Ehdr + Ehdr->e_shoff);
	CHAR8 *ShStrTab =
		(CHAR8 *)((UINT8 *)Ehdr + Shdr[Ehdr->e_shstrndx].sh_offset);

	for (INTN I = 0; I < Ehdr->e_phnum; ++I) {
		// only look at PT_LOAD segments
		if (Phdr[I].p_type == PT_LOAD) {
			UINT64 SegmentPageCount =
				((Phdr[I].p_memsz - 1) / PAGE_SIZE) + 1;
			EFI_PHYSICAL_ADDRESS SegmentPhysicalAddress = 0;
			Status = SystemTable->BootServices->AllocatePages(
				AllocateAnyPages, KEEP_AFTER_BOOT,
				SegmentPageCount, &SegmentPhysicalAddress);
			if (EFI_ERROR(Status)) {
				SystemTable->ConOut->OutputString(
					SystemTable->ConOut,
					L"[Boot] Failed to Allocate Kernel Load Segment!\n");
				return EFI_OUT_OF_RESOURCES;
			}

			Memcpy((VOID *)SegmentPhysicalAddress,
			       ((UINT8 *)Ehdr + Phdr[I].p_offset),
			       Phdr[I].p_memsz);

			Status = Map_Memory(SystemTable, UpperPageTable,
					    (Phdr[I].p_vaddr),
					    SegmentPhysicalAddress, 0,
					    SegmentPageCount);
			if (EFI_ERROR(Status)) {
				SystemTable->ConOut->OutputString(
					SystemTable->ConOut,
					L"[Boot] Failed to Map Kernel Load Segment!\n");
				return EFI_LOAD_ERROR;
			}
		}
	}

	for (INTN I = 0; I < Ehdr->e_shnum; ++I) {
		CONST CHAR8 *SectionName = (CHAR8 *)ShStrTab + Shdr[I].sh_name;

		if (Strncmp(SectionName, ".export", 7) == 0) {
			CONST CHAR8 *TypeString = SectionName + 8;
			CHAR8 *Dot = StrChr(TypeString, '.');

			if (Dot) {
				*Dot = '\0';
				CHAR8 *NameString = Dot + 1;
				VOID *VTableAddress = (VOID *)(Shdr[I].sh_addr);

				MODULE_META ModuleMetadata;
				ModuleMetadata.VTablePtr = VTableAddress;
				ModuleMetadata.Type = TypeString;
				ModuleMetadata.Name = NameString;
				ModuleMetadata.VTableSize = Shdr[I].sh_size;
				RegistryPut(ModuleMetadata);
			}
		} else if (Strncmp(SectionName, ".import", 7) == 0) {
			CONST CHAR8 *TypeString = SectionName + 8;
			CHAR8 *Dot = StrChr(TypeString, '.');

			CHAR8 *NameString = 0;
			if (Dot) {
				*Dot = '\0';
				NameString = Dot + 1;
			}

			MODULE_IMPORT_HANDLE ImportHandle;
			ImportHandle.TypeString = TypeString;
			ImportHandle.NameString = NameString;
			ImportHandle.VTablePtr = (VOID *)(Shdr[I].sh_addr);

			AddModuleImportHandle(SystemTable, ImportHandle);
		}
	}

	*Entry = Ehdr->e_entry;

	return EFI_SUCCESS;
}
