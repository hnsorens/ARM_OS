#include <efi.h>
#include <efilib.h>

#include "boot_services.h"
#include "filesystem.h"
#include "page_table.h"
#include "elf_loader.h"
#include "serial.h"
#include "bootinfo.h"
#include "kernel_loader.h"
#include "module_registry.h"
#include "module_import_handle.h"

#define STACK_SIZE_PAGES 0x100

VOID Jump_To_Kernel(EFI_VIRTUAL_ADDRESS Entry, EFI_VIRTUAL_ADDRESS BootInfoPtr,
		    EFI_VIRTUAL_ADDRESS Stack)
{
	asm volatile("mov x0, %0\n\t" // First arg in x0
		     "mov sp, %1\n\t" // Set stack pointer
		     "br %2" // Jump
		     :
		     : "r"(BootInfoPtr), // %0
		       "r"(Stack), // %1
		       "r"(Entry) // %2
		     : "x0", "memory" // Remove "sp" from here
	);
}

EFI_STATUS
EFIAPI
efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	EFI_STATUS Status;

	Boot_Log_Start();

	EFI_FILE_PROTOCOL *Root = NULL;
	Status = OpenRoot(SystemTable, ImageHandle, &Root);
	if (EFI_ERROR(Status)) {
		Boot_Log("Failed to open root\n", 20);
		return Status;
	}
	Boot_Log("Opened root\n", 12);

	// Read kernel elf file
	//CHAR8 *Buffer;
	//ReadFile(L"\\kernel.bin", Root, SystemTable, ImageHandle, &Buffer);

	VOID *ModuleRegistryBlock = 0;
	SystemTable->BootServices->AllocatePool(
		EfiRuntimeServicesCode, 1024 * 1024, &ModuleRegistryBlock);
	RegistryInit((VOID *)ModuleRegistryBlock, 1024 * 1024, 10);

	ModuleImportHandleInit(SystemTable);

	PAGE_TABLE_T LowerPageTable = 0;
	PAGE_TABLE_T UpperPageTable = 0;

	EFI_VIRTUAL_ADDRESS Entry;
	Load_Kernel(SystemTable, ImageHandle, Root, L"\\kernel.ini", &Entry,
		    &UpperPageTable);

	if (EFI_ERROR(Status)) {
		Boot_Log("Failed to load kernel\n", 22);
		return Status;
	}
	Boot_Log("Loaded kernel\n", 14);

	// Free loaded elf file after its use is finished
	//SystemTable->BootServices->FreePool(Buffer);

	// Allocate Boot Info Struct
	BootInfoStruct *BootInfo = 0;
	Status = SystemTable->BootServices->AllocatePool(EfiRuntimeServicesCode,
							 sizeof(BootInfoStruct),
							 (VOID **)&BootInfo);
	if (EFI_ERROR(Status)) {
		Boot_Log("Failed to allocate boot info\n", 29);
		return Status;
	}
	Boot_Log("Allocated boot info struct\n", 27);

	// Allocate Stack
	EFI_PHYSICAL_ADDRESS StackPhysicalAddress = 0;
	Status = SystemTable->BootServices->AllocatePages(
		AllocateAnyPages, EfiRuntimeServicesCode, STACK_SIZE_PAGES,
		&StackPhysicalAddress);
	if (EFI_ERROR(Status)) {
		Boot_Log("Failed to allocate stack\n", 25);
		return Status;
	}
	Boot_Log("Allocated Stack\n", 16);

	Status = Map_Memory(SystemTable, &UpperPageTable, 0xFFFF800000000000,
			    StackPhysicalAddress, 0, STACK_SIZE_PAGES);
	if (EFI_ERROR(Status)) {
		Boot_Log("Failed to map stack to upper page table\n", 40);
		return Status;
	}
	Boot_Log("Mapped stack to upper page table\n", 33);

	// Create an identity page table for the bottom half of memory
	Status = Create_Identity_Page_Table(SystemTable, 10, &LowerPageTable);

	if (EFI_ERROR(Status)) {
		Boot_Log("Failed to create lower identity page table\n", 43);
		return Status;
	}
	Boot_Log("Created lower identity page table\n", 34);

	MEMORY_MAP MemoryMap;
	UINTN MemoryMapRegionsCount;
	Status = ExitBootServices(ImageHandle, SystemTable, &MemoryMap,
				  &MemoryMapRegionsCount);
	if (EFI_ERROR(Status)) {
		Boot_Log("Failed to exit boot services\n", 28);
		return Status;
	}

	Boot_Log("Exited boot services\n", 21);

	// Enable page tables
	Enable_Page_Table(LowerPageTable, UpperPageTable);

	// Do this after page table are enabled, so that the code is accessible in memory
	HandleModuleImports();

	BootInfo->memoryMapSize = MemoryMapRegionsCount;
	BootInfo->memoryRegions = (MemoryRegion *)MemoryMap;

	Jump_To_Kernel(Entry, (EFI_VIRTUAL_ADDRESS)BootInfo,
		       0xFFFF800000000000 + (4096 * STACK_SIZE_PAGES));

	while (1) {
		__asm__ volatile("wfi");
	}
	return EFI_SUCCESS;
}
