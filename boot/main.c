#include "memory_constants.h"
#include <efi.h>
#include <efilib.h>

#include "boot_services.h"
#include "filesystem.h"
#include "page_table.h"
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
		Fail_Log("Opening Root\n", 13);
		return Status;
	}
	Ok_Log("Opening Root\n", 13);

	// Read kernel elf file
	//CHAR8 *Buffer;
	//ReadFile(L"\\kernel.bin", Root, SystemTable, ImageHandle, &Buffer);

	VOID *ModuleRegistryBlock = 0;
	Status = SystemTable->BootServices->AllocatePool(
		KEEP_AFTER_BOOT, 1024 * 1024, &ModuleRegistryBlock);
	if (EFI_ERROR(Status)) {
		Fail_Log("Allocating module registry block\n", 33);
		return Status;
	}
	Ok_Log("Allocating module registry block\n", 33);

	Status = RegistryInit((VOID *)ModuleRegistryBlock, 1024 * 1024, 10);
	if (EFI_ERROR(Status)) {
		Fail_Log("Initializing module registry\n", 29);
		return Status;
	}
	Ok_Log("Initializing module registry\n", 29);

	Status = ModuleImportHandleInit(SystemTable);
	if (EFI_ERROR(Status)) {
		Fail_Log("Initializing module import handles\n", 35);
		return Status;
	}
	Ok_Log("Initializing module import handles\n", 35);

	PAGE_TABLE_T LowerPageTable = 0;
	PAGE_TABLE_T UpperPageTable = 0;

	EFI_VIRTUAL_ADDRESS Entry;
	Status = Load_Kernel(SystemTable, ImageHandle, Root, L"\\kernel.ini",
			     &Entry, &UpperPageTable);

	if (EFI_ERROR(Status)) {
		Fail_Log("Loading kernel\n", 15);
		return Status;
	}
	Ok_Log("Loading kernel\n", 15);

	// Allocate Boot Info Struct
	BootInfoStruct *BootInfo = 0;
	Status = SystemTable->BootServices->AllocatePool(
		KEEP_AFTER_BOOT, sizeof(BootInfoStruct), (VOID **)&BootInfo);
	if (EFI_ERROR(Status)) {
		Fail_Log("Allocating boot info\n", 21);
		return Status;
	}
	Ok_Log("Allocating boot info\n", 21);

	// Allocate Stack
	EFI_PHYSICAL_ADDRESS StackPhysicalAddress = 0;
	Status = SystemTable->BootServices->AllocatePages(
		AllocateAnyPages, KEEP_AFTER_BOOT, STACK_SIZE_PAGES,
		&StackPhysicalAddress);
	if (EFI_ERROR(Status)) {
		Fail_Log("Allocating stack\n", 17);
		return Status;
	}
	Ok_Log("Allocating stack\n", 17);

	Status = Map_Memory(SystemTable, &UpperPageTable, 0xFFFF800000000000,
			    StackPhysicalAddress, 0, STACK_SIZE_PAGES);
	if (EFI_ERROR(Status)) {
		Fail_Log("Mapping stack memory\n", 21);
		return Status;
	}
	Ok_Log("Mapping stack memory\n", 21);

	// Create an identity page table for the bottom half of memory
	Status = Create_Identity_Page_Table(SystemTable, 10, &LowerPageTable);
	if (EFI_ERROR(Status)) {
		Fail_Log("Creating lower identity page table\n", 35);
		return Status;
	}
	Ok_Log("Creating lower identity page table\n", 35);

	MEMORY_MAP MemoryMap;
	UINTN MemoryMapRegionsCount;
	Status = ExitBootServices(ImageHandle, SystemTable, &MemoryMap,
				  &MemoryMapRegionsCount);
	if (EFI_ERROR(Status)) {
		Fail_Log("Exiting boot services\n", 22);
		return Status;
	}
	Ok_Log("Exiting boot services\n", 22);

	// Enable page tables
	Status = Enable_Page_Table(LowerPageTable, UpperPageTable);
	if (EFI_ERROR(Status)) {
		Fail_Log("Enabling page table\n", 20);
		return Status;
	}
	Ok_Log("Enabling page table\n", 20);

	// Do this after page table are enabled, so that the code is accessible in memory
	Status = HandleModuleImports();
	if (EFI_ERROR(Status)) {
		Fail_Log("Handling module imports\n", 24);
		return Status;
	}
	Ok_Log("Handling module imports\n", 24);

	Status = InitializeModules();
	if (EFI_ERROR(Status)) {
		Fail_Log("Initializing modules\n", 21);
		return Status;
	}
	Ok_Log("Initializing modules\n", 21);

	Ok_Log("Boot successful\n", 16);

	BootInfo->memoryMapSize = MemoryMapRegionsCount;
	BootInfo->memoryRegions = (MemoryRegion *)MemoryMap;

	//Jump_To_Kernel(Entry, (EFI_VIRTUAL_ADDRESS)BootInfo,
	//       0xFFFF800000000000 + (4096 * STACK_SIZE_PAGES));

	while (1) {
		__asm__ volatile("wfi");
	}
	return EFI_SUCCESS;
}
