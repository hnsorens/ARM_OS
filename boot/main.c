#include <efi.h>
#include <efilib.h>

#include "boot_services.h"
#include "filesystem.h"
#include "page_table.h"
#include "elf_loader.h"

EFI_STATUS
EFIAPI
efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {

    EFI_STATUS Status;

    EFI_FILE_PROTOCOL* Root = NULL;
    Status = OpenRoot(SystemTable, ImageHandle, &Root);
    if (EFI_ERROR(Status))
    {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[Boot] Failed to Open Root!\n");
        return Status;
    }
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[Boot] Opened Root!\n");

    // Read kernel elf file
    CHAR8* Buffer;
    ReadFile(L"\\kernel.bin", Root, SystemTable, ImageHandle, &Buffer);

    PAGE_TABLE_T LowerPageTable;
    PAGE_TABLE_T UpperPageTable;

    Status = Load_Kernel(SystemTable, Buffer, &UpperPageTable);
    if (EFI_ERROR(Status))
    {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[Boot] Failed to Load Kernel!\n");
        return Status;
    }
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[Boot] Loaded Kernel!\n");

    // Free loaded elf file after its use is finished
    SystemTable->BootServices->FreePool(Buffer);

    // Create an identity page table for the bottom half of memory
    Status = Create_Identity_Page_Table(SystemTable, 10, &LowerPageTable);
    if (EFI_ERROR(Status))
    {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[Boot] Failed to create Lower Identity Page Table!\n");
        return Status;
    }
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[Boot] Created Lower Identity Page Table!\n");

    MEMORY_MAP MemoryMap;
    UINTN MemoryMapRegionsCount;
    Status = ExitBootServices(ImageHandle, SystemTable, &MemoryMap, &MemoryMapRegionsCount);
    if (EFI_ERROR(Status))
    {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[Boot] Failed to Exit Boot Services!\n");
        return Status;
    }
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[Boot] Exited Boot Services!\n");

    // Enable page tables
    Enable_Page_Table(LowerPageTable, UpperPageTable);

    while(1) { __asm__ volatile("wfi"); }
    return EFI_SUCCESS;
}
