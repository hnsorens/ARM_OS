#include "elf_loader.h"

#define PAGE_SIZE 4096

static BOOLEAN
Verify_Elf(Elf64_Ehdr *Header)
{
    return Header->e_ident[0] == 0x7F &&
           Header->e_ident[1] == 'E'  &&
           Header->e_ident[2] == 'L'  &&
           Header->e_ident[3] == 'F';
}

EFI_STATUS
Load_Kernel(
        IN EFI_SYSTEM_TABLE *SystemTable,
        IN CHAR8 *KernelElfBuffer,
        OUT PAGE_TABLE_T *UpperPageTable
)
{
    EFI_STATUS Status;

    Elf64_Ehdr *Ehdr = (Elf64_Ehdr*)KernelElfBuffer;

    // Verify that Kernel is an Elf File
    if (!Verify_Elf(Ehdr))
    {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[Boot] Kernel Not a Valid Elf!\n");
        return EFI_LOAD_ERROR;
    }

    Elf64_Phdr *Phdr = (Elf64_Phdr*)((UINT8*)Ehdr + Ehdr->e_phoff);

    for (INTN I = 0; I < Ehdr->e_phnum; ++I)
    {
        // only look at PT_LOAD segments
        if (Phdr[I].p_type == PT_LOAD)
        {
            UINT64 SegmentPageCount = ((Phdr[I].p_memsz - 1) / PAGE_SIZE) + 1;
            EFI_PHYSICAL_ADDRESS SegmentPhysicalAddress = 0;
            Status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiRuntimeServicesCode, SegmentPageCount, &SegmentPhysicalAddress);
            if (EFI_ERROR(Status))
            {
                SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[Boot] Failed to Allocate Kernel Load Segment!\n");
                return EFI_OUT_OF_RESOURCES;
            }

            Status = Map_Memory(SystemTable, UpperPageTable, Phdr[I].p_vaddr, SegmentPhysicalAddress, 0, SegmentPageCount);
            if (EFI_ERROR(Status))
            {
                SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[Boot] Failed to Map Kernel Load Segment!\n");
                return EFI_LOAD_ERROR;
            }
        }
    }

    return EFI_SUCCESS;
}


