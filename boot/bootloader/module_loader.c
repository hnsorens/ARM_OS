
#include "module_loader.h"
#include "Base.h"
#include "Guid/FileInfo.h"
#include "ProcessorBind.h"
#include "Protocol/DevicePath.h"
#include "Protocol/SimpleFileSystem.h"
#include "Uefi/UefiBaseType.h"
#include "Uefi/UefiMultiPhase.h"
#include "Uefi/UefiSpec.h"

#include "elf.h"
#include <linux/limits.h>

#define PRINT(str) SystemTable->ConOut->OutputString(SystemTable->ConOut, str);

#define BREAK while(1);
#define LOG(reg, val) __asm__ volatile("mov " #reg ", %0" :: "r"((unsigned long)val));

#define MAX_MODULE_COUNT 128

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
      AllocateAnyPages, EfiRuntimeServicesCode, ModuleMemorySize, &ModuleMemory);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  EFI_PHYSICAL_ADDRESS ModuleWriteLocation;
  for (int i = 0; i < Count; i++) {
    File = Modules[i].File;
    UINTN FileSize = Modules[i].Info->FileSize;

    Status = SystemTable->BootServices->AllocatePages(
      AllocateAnyPages, EfiRuntimeServicesCode, ((Modules[i].Info->FileSize + 4095) / 4096), &ModuleWriteLocation);


    Modules[i].ModuleBase = ModuleWriteLocation;
    Status = File->Read(File, &FileSize, (VOID *)ModuleWriteLocation);

    SystemTable->BootServices->FreePool(Modules[i].Info);
    File->Close(File);
  }

  return EFI_SUCCESS;
}

#define MODULE_ENTRY(Path, Name, Type) {Path, Name, Type, 0, 0, 0, 0}

UINTN strlen(CHAR8 *str) {
    UINTN len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}

CHAR16* ConvertString(EFI_SYSTEM_TABLE *SystemTable, CHAR8* str)
{
  UINTN StringSize = strlen(str) + 1;
  EFI_PHYSICAL_ADDRESS String;

  SystemTable->BootServices->AllocatePool(
      EfiLoaderData, StringSize * 2, (VOID**)(&String));

  for (int i = 0; i < StringSize; i++)
  {
    ((CHAR16*)String)[i] = str[i];
  }

  return (CHAR16*)String;

}

int Strcmp(char *s1, char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

MODULE_TYPE GetModuleType(CHAR8* Type)
{
  for (int i = 0; i < sizeof(MODULE_NAMES) / sizeof(CHAR8*); i++)
  {
    if (!Strcmp(MODULE_NAMES[i], Type))
    {
      return i;
    }
  }
  return -1;
}

EFI_STATUS
OpenFile(
        IN CHAR16 *FileName,
        IN EFI_FILE_PROTOCOL *Root,
        IN EFI_SYSTEM_TABLE *SystemTable,
        OUT CHAR8 **Buffer
)
{
    EFI_FILE_PROTOCOL *File = NULL;
    EFI_FILE_INFO *FileInfo = NULL;
    CHAR8 *FileBuffer = NULL;

    EFI_STATUS Status;
    Status = Root->Open(Root, &File, FileName, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    // Get File info to determine size
    UINTN InfoSize = sizeof(EFI_FILE_INFO) + 128;
    Status = SystemTable->BootServices->AllocatePool(EfiLoaderData, InfoSize, (VOID **)&FileInfo);
    if (EFI_ERROR(Status)) {
        File->Close(File);
        return Status;
    }

    Status = File->GetInfo(File, &gEfiFileInfoGuid, &InfoSize, FileInfo);
    if (EFI_ERROR(Status)) {
        SystemTable->BootServices->FreePool(FileInfo);
        File->Close(File);
        return Status;
    }

      // Allocate buffer for file content + null terminator
      Status = SystemTable->BootServices->AllocatePool(
          EfiLoaderData, FileInfo->FileSize + 1, (VOID**)&FileBuffer);
      if (EFI_ERROR(Status)) {
        SystemTable->BootServices->FreePool(FileInfo);
        File->Close(File);
        return Status;
      }

      // Read the entire file
      UINTN ReadSize = FileInfo->FileSize;
      Status = File->Read(File, &ReadSize, FileBuffer);
      if (EFI_ERROR(Status) || ReadSize != FileInfo->FileSize) {
        SystemTable->BootServices->FreePool(FileBuffer);
        SystemTable->BootServices->FreePool(FileInfo);
        File->Close(File);
        return EFI_LOAD_ERROR;
      }
      
      // Null-terminate the buffer
      FileBuffer[FileInfo->FileSize] = '\0';
      
      // Clean up file resources
      SystemTable->BootServices->FreePool(FileInfo);
      File->Close(File);
      FileInfo = NULL;
      File = NULL;

      *Buffer = FileBuffer;

      return Status;
}

BOOLEAN
VerifyElf(Elf64_Ehdr *header) {
    return header->e_ident[0] == 0x7F && 
            header->e_ident[1] == 'E' && 
            header->e_ident[2] == 'L' &&
            header->e_ident[3] == 'F';
}

EFI_STATUS
CalculateElfSpan(Elf64_Ehdr *Ehdr, UINT64 *MinAddr, UINT64 *MaxAddr) {
    Elf64_Phdr *Phdr = (Elf64_Phdr *)((UINT8 *)Ehdr + Ehdr->e_phoff);

    UINT64 MinVaddr = 0xFFFFFFFFFFFFFFFF;
    UINT64 MaxVaddr = 0;
    BOOLEAN FoundLoadable = 0;

    for (INTN i = 0; i < Ehdr->e_phnum; ++i) {
        // only look at PT_LOAD segments
        if (Phdr[i].p_type == PT_LOAD) {
            if (Phdr[i].p_vaddr < MinVaddr) {
                MinVaddr = Phdr[i].p_vaddr;
            }
            UINT64 EndVaddr = Phdr[i].p_vaddr + Phdr[i].p_memsz;
            if (EndVaddr > MaxVaddr) {
                MaxVaddr = EndVaddr;
            }
            FoundLoadable = TRUE;
        }
    }

    if (!FoundLoadable) return EFI_LOAD_ERROR;
    
    *MinAddr = MinVaddr;
    *MaxAddr = MaxVaddr;

    return EFI_SUCCESS;
}

VOID*
Memcpy(VOID *Dest, CONST VOID *Src, UINTN N) {
    UINT8 *D = (UINT8 *)Dest;
    CONST UINT8 *S = (CONST UINT8 *)Src;

    while (N--) {
        *D++ = *S++;
    }
    return Dest;
}

VOID*
Memset(VOID *S, UINT8 C, UINTN N) {
    UINT8 *P = (UINT8 *)S;

    while (N--) {
        *P++ = C;
    }
    return S;
}

EFI_STATUS
OpenRoot(
        IN EFI_SYSTEM_TABLE *SystemTable,
        OUT EFI_FILE_PROTOCOL **Root
) {
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;

  EFI_STATUS Status = SystemTable->BootServices->LocateProtocol(
      &gEfiSimpleFileSystemProtocolGuid, NULL, (VOID **)&FileSystem);
  if (EFI_ERROR(Status)) {
    SystemTable->ConOut->OutputString(SystemTable->ConOut,
                                      L"Failed to find filesystem protocol!");
    return Status;
  }

  Status = FileSystem->OpenVolume(FileSystem, Root);
  if (EFI_ERROR(Status)) {
    SystemTable->ConOut->OutputString(SystemTable->ConOut,
                                      L"Failed to open root volume!");
    return Status;
  }
}

EFI_STATUS
LoadKernel(
  IN   EFI_SYSTEM_TABLE *SystemTable,
  OUT  VOID **Kernel
)
{
    EFI_FILE_PROTOCOL *Root = NULL;
    EFI_STATUS Status = OpenRoot(SystemTable, &Root);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    // Open kernel elf
    CHAR8 *FileBuffer = NULL;
    Status = OpenFile(L"kernel.elf", Root, SystemTable, &FileBuffer);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    // Get Elf Buffer
    Elf64_Ehdr *Ehdr = (Elf64_Ehdr*)FileBuffer;

    // Verify elf file
    if (!VerifyElf(Ehdr)) return EFI_LOAD_ERROR;

    // Calculate size
    UINT64 MaxAddr = 0;
    UINT64 MinAddr = 0;
    Status = CalculateElfSpan(Ehdr, &MinAddr, &MaxAddr);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    UINT64 KernelSize = MaxAddr - MinAddr;
    VOID *KernelData = NULL;

    // Allocate Kernel Space
    Status = SystemTable->BootServices->AllocatePool(
        EfiLoaderData, KernelSize, &KernelData);
    if (EFI_ERROR(Status)) {
        SystemTable->BootServices->FreePool(KernelData);
        return Status;
    }

    Elf64_Phdr *Phdr = (Elf64_Phdr *)((UINT8 *)Ehdr + Ehdr->e_phoff);

    // Load Sections
    for (INTN i = 0; i < Ehdr->e_phnum; ++i) {
        // only look at PT_LOAD segments
        if (Phdr[i].p_type == PT_LOAD) {
            UINT64 RelativeOffset = Phdr[i].p_vaddr - MinAddr;
            CHAR8 *Dest = (CHAR8 *)KernelData + RelativeOffset;

            CHAR8 *Src = (CHAR8 *)Ehdr + Phdr[i].p_offset;
            Memcpy(Dest, Src, Phdr[i].p_filesz);

            if (Phdr[i].p_memsz > Phdr[i].p_filesz) {
                Memset(Dest + Phdr[i].p_filesz, 0, Phdr[i].p_memsz - Phdr[i].p_filesz);
            }
        }
    }

    *Kernel = KernelData;

    return EFI_SUCCESS;
}
