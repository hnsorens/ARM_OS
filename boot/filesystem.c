#include "filesystem.h"

static EFI_GUID gEfiSimpleFileSystemProtocolGuid = {
	0x964e5b22,
	0x6459,
	0x11d2,
	{ 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
};
static EFI_GUID gEfiFileInfoGuid = { 0x09576e92,
				     0x6d3f,
				     0x11d2,
				     { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72,
				       0x3b } };

EFI_STATUS
OpenRoot(IN EFI_SYSTEM_TABLE *ST, IN EFI_HANDLE ImageHandle,
	 OUT EFI_FILE_PROTOCOL **Root)
{
	UINTN HandleCount = 0;
	EFI_HANDLE *Handles = NULL;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FS = NULL;
	EFI_STATUS Status;

	// Search the ENTIRE system for every handle that supports a filesystem
	Status = ST->BootServices->LocateHandleBuffer(
		ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL,
		&HandleCount, &Handles);

	if (Status != 0 || HandleCount == 0) {
		Boot_Log("No Fat32 volumes found in system\n", 33);

		return Status;
	}

	// Try the first handle found (this is usually your boot disk)
	Status = ST->BootServices->HandleProtocol(
		Handles[0], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&FS);

	if (Status == 0) {
		Status = FS->OpenVolume(FS, Root);
	}

	// Clean up the memory allocated by LocateHandleBuffer
	ST->BootServices->FreePool(Handles);

	return Status;
}

EFI_STATUS
ReadFile(IN CHAR16 *FileName, IN EFI_FILE_PROTOCOL *Root,
	 IN EFI_SYSTEM_TABLE *SystemTable, IN EFI_HANDLE ImageHandle,
	 OUT CHAR8 **Buffer)
{
	EFI_FILE_PROTOCOL *File = NULL;
	EFI_FILE_INFO *FileInfo = NULL;
	CHAR8 *FileBuffer = NULL;

	EFI_STATUS Status;
	Status = Root->Open(Root, &File, FileName, EFI_FILE_MODE_READ, 0);
	if (EFI_ERROR(Status)) {
		Boot_Log("Failed to open root\n", 20);
		return Status;
	}
	Boot_Log("Opened Root\n", 12);

	// Get File info to determine size
	UINTN InfoSize = sizeof(EFI_FILE_INFO) + 128;
	Status = SystemTable->BootServices->AllocatePool(
		EfiLoaderData, InfoSize, (VOID **)&FileInfo);
	if (EFI_ERROR(Status)) {
		Boot_Log("Failed to allocate kernel file info\n", 36);
		File->Close(File);
		return Status;
	}
	Boot_Log("Allocated kernel file info\n", 27);

	Status = File->GetInfo(File, &gEfiFileInfoGuid, &InfoSize, FileInfo);
	if (EFI_ERROR(Status)) {
		Boot_Log("Failed to get file info\n", 24);
		SystemTable->BootServices->FreePool(FileInfo);
		File->Close(File);
		return Status;
	}
	Boot_Log("Found kernel file info\n", 23);

	// Allocate buffer for file content + null terminator
	Status = SystemTable->BootServices->AllocatePool(
		EfiLoaderData, FileInfo->FileSize + 1, (VOID **)&FileBuffer);
	if (EFI_ERROR(Status)) {
		Boot_Log("Failed to allocate kernel space\n", 32);
		SystemTable->BootServices->FreePool(FileInfo);
		File->Close(File);
		return Status;
	}
	Boot_Log("Allocated kernel space\n", 23);

	// Read the entire file
	UINTN ReadSize = FileInfo->FileSize;
	Status = File->Read(File, &ReadSize, FileBuffer);
	if (EFI_ERROR(Status) || ReadSize != FileInfo->FileSize) {
		Boot_Log("Failed to read file\n", 20);
		SystemTable->BootServices->FreePool(FileBuffer);
		SystemTable->BootServices->FreePool(FileInfo);
		File->Close(File);
		return Status;
	}
	Boot_Log("Read Kernel\n", 12);

	// Null-terminate the buffer
	FileBuffer[FileInfo->FileSize] = '\0';

	// Clean up file resources
	SystemTable->BootServices->FreePool(FileInfo);
	File->Close(File);
	FileInfo = NULL;
	File = NULL;

	*Buffer = FileBuffer;

	return EFI_SUCCESS;
}
