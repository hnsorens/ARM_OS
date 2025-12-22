
#include "module_loader.h"
#include "ProcessorBind.h"
#include "Protocol/SimpleFileSystem.h"
#include "Uefi/UefiBaseType.h"
#include "Uefi/UefiMultiPhase.h"
#include "Uefi/UefiSpec.h"

static 
EFI_STATUS 
LoadModule(
  IN EFI_SYSTEM_TABLE *SystemTable, 
  IN EFI_FILE_PROTOCOL *Root,                    
  IN MODULE_LOAD *Modules, 
  IN UINTN Count
) {
  EFI_STATUS Status;
  EFI_FILE_PROTOCOL *File;

  UINT64 ModuleMemorySize = 0;
  for (int i = 0; i < Count; i++) {
    Status =
        Root->Open(Root, &File, Modules[i].ModulePath, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status)) {
      Root->Close(Root);
      return Status;
    }

    EFI_FILE_INFO *FileInfo;
    UINTN InfoSize = sizeof(EFI_FILE_INFO) + 128;

    Status = SystemTable->BootServices->AllocatePool(EfiLoaderData, InfoSize,
                                                     (VOID **)&FileInfo);
    if (EFI_ERROR(Status)) {
      File->Close(File);
      Root->Close(Root);
      return Status;
    }

    Status = File->GetInfo(File, &gEfiFileInfoGuid, &InfoSize, FileInfo);
    if (EFI_ERROR(Status)) {
      SystemTable->BootServices->FreePool(FileInfo);
      File->Close(File);
      Root->Close(Root);
      return Status;
    }

    ModuleMemorySize += (FileInfo->Size + 4095) / 4096;

    Modules[i].Info = FileInfo;
    Modules[i].File = File;
  }

  EFI_PHYSICAL_ADDRESS ModuleMemory;

  Status = SystemTable->BootServices->AllocatePages(
      AllocateAnyPages, EfiRuntimeServicesData, ModuleMemorySize, &ModuleMemory);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  EFI_PHYSICAL_ADDRESS ModuleWriteLocation;
  for (int i = 0; i < Count; i++) {
    File = Modules[i].File;
    UINTN FileSize = Modules[i].Info->FileSize;

    Status = SystemTable->BootServices->AllocatePages(
      AllocateAnyPages, EfiRuntimeServicesData, ((Modules[i].Info->FileSize + 4095) / 4096), &ModuleWriteLocation);

    Modules[i].ModuleBase = ModuleWriteLocation;
    Status = File->Read(File, &FileSize, (VOID *)ModuleWriteLocation);

    SystemTable->BootServices->FreePool(Modules[i].Info);
    File->Close(File);
  }

  return EFI_SUCCESS;
}

#define MODULE_ENTRY(Path, Name, Type) {Path, Name, Type, 0, 0, 0, 0}

MODULE_LOAD Modules[] = {
  MODULE_ENTRY(L"\\kernel_core.efi", L"Kernel", ModuleKernelCore),
  MODULE_ENTRY(L"\\pmm.efi", L"PhysicalMemoryManager", ModulePmm),
  MODULE_ENTRY(L"\\vmm.efi", L"VirtualMemoryManager", ModuleVmm),
  MODULE_ENTRY(L"\\kmm.efi", L"KernelMemoryManager", ModuleKmm),
  MODULE_ENTRY(L"\\gic.efi", L"GIC", ModuleGic)
};

EFI_STATUS
LoadModules(EFI_SYSTEM_TABLE *SystemTable, MODULE_TABLE* ModuleTable)
{
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
  EFI_FILE_PROTOCOL *Root;

  EFI_STATUS Status = SystemTable->BootServices->LocateProtocol(
      &gEfiSimpleFileSystemProtocolGuid, NULL, (VOID **)&FileSystem);
  if (EFI_ERROR(Status)) {
    SystemTable->ConOut->OutputString(SystemTable->ConOut,
                                      L"Failed to find filesystem protocol!");
    return Status;
  }

  Status = FileSystem->OpenVolume(FileSystem, &Root);
  if (EFI_ERROR(Status)) {
    SystemTable->ConOut->OutputString(SystemTable->ConOut,
                                      L"Failed to open root volume!");
    return Status;
  }


  UINTN ModuleCount = sizeof(Modules) / sizeof(MODULE_LOAD);
  Status = LoadModule(SystemTable, Root, Modules, ModuleCount);

  Root->Close(Root);

  EFI_PHYSICAL_ADDRESS ModuleListAddr;
  SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiRuntimeServicesData, 1, &ModuleListAddr);

  MODULE* ModuleList = (MODULE*)ModuleListAddr;
  for (int i = 0; i < ModuleCount; i++)
  {
    CHAR16* NameSrc = Modules[i].ModuleName;
    char* NameDst = ModuleList[i].ModuleName;
    do {
      *NameDst = *NameSrc;
      NameSrc++;
      NameDst++;
    }while (*NameSrc);
    
    ModuleList[i].ModuleBase = Modules[i].ModuleBase;
    ModuleList[i].VTable = Modules[i].VTable;
    ModuleList[i].Size = Modules[i].Info->FileSize;
    ModuleList[i].Type = Modules[i].Type;
  }

  ModuleTable->ModuleCount = ModuleCount;
  ModuleTable->Modules = ModuleList;
  return Status;
}
