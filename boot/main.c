#include <efi.h>
#include <efilib.h>

#include "boot_services.h"
#include "filesystem.h"
#include "page_table.h"
#include "elf_loader.h"
#include "serial.h"

void IntToHexStr(UINT64 Value, CHAR16* Buffer) {
    CHAR16 *HexChars = L"0123456789ABCDEF";
    int i;

    // Optional: Add hex prefix
    Buffer[0] = L'0';
    Buffer[1] = L'x';

    // Process 16 nibbles (64-bit) from right to left
    for (i = 15; i >= 0; i--) {
        Buffer[i + 2] = HexChars[Value & 0xF];
        Value >>= 4;
    }

    Buffer[18] = L'\0'; // Null terminator
}

#define PRINT_NUMBER(label, number) {CHAR16 Buffer[50]; IntToHexStr((number), Buffer); SystemTable->ConOut->OutputString(SystemTable->ConOut, (label)); SystemTable->ConOut->OutputString(SystemTable->ConOut, Buffer); SystemTable->ConOut->OutputString(SystemTable->ConOut, L"\n");}

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

    PRINT_NUMBER(L"Page Table", LowerPageTable);

    if (EFI_ERROR(Status))
    {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[Boot] Failed to create Lower Identity Page Table!\n");
        return Status;
    }
    SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[Boot] Created Lower Identity Page Table!\n");

    serial_debug_start();

    MEMORY_MAP MemoryMap;
    UINTN MemoryMapRegionsCount;
    Status = ExitBootServices(ImageHandle, SystemTable, &MemoryMap, &MemoryMapRegionsCount);
    if (EFI_ERROR(Status))
    {
        SystemTable->ConOut->OutputString(SystemTable->ConOut, L"[Boot] Failed to Exit Boot Services!\n");
        return Status;
    }

    serial_debug_serial_printf("[Boot] Exited Boot Services!\n");

    // Enable page tables
    Enable_Page_Table(LowerPageTable, UpperPageTable);

    while(1) { __asm__ volatile("wfi"); }
    return EFI_SUCCESS;
}
