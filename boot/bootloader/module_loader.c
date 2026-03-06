
#include "module_loader.h"
#include "Base.h"
#include "ProcessorBind.h"
#include "Protocol/SimpleFileSystem.h"
#include "Uefi/UefiBaseType.h"
#include "Uefi/UefiMultiPhase.h"
#include "Uefi/UefiSpec.h"

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

// EFI_STATUS
// GetModulesToLoad(
//   IN  EFI_SYSTEM_TABLE *SystemTable, 
//   IN  EFI_FILE_PROTOCOL *Root, 
//   OUT UINTN* ModuleCount,
//   OUT MODULE_LOAD** Modules)
// {

//   *ModuleCount = 0;

//   EFI_STATUS Status;
//   EFI_FILE_PROTOCOL *File;

//   Status =
//         Root->Open(Root, &File, L"\\modules.conf", EFI_FILE_MODE_READ, 0);
//   if (EFI_ERROR(Status)) {
//     Root->Close(Root);
//     return Status;
//   }
  
//   EFI_FILE_INFO *FileInfo;
//   UINTN InfoSize = sizeof(EFI_FILE_INFO) + 128;

//   Status = SystemTable->BootServices->AllocatePool(EfiLoaderData, InfoSize,
//                                                     (VOID **)&FileInfo);
//   if (EFI_ERROR(Status)) {
//     File->Close(File);
//     Root->Close(Root);
//     return Status;
//   }

//   Status = File->GetInfo(File, &gEfiFileInfoGuid, &InfoSize, FileInfo);
//   if (EFI_ERROR(Status)) {
//     SystemTable->BootServices->FreePool(FileInfo);
//     File->Close(File);
//     Root->Close(Root);
//     return Status;
//   }

//   EFI_PHYSICAL_ADDRESS ModuleMemory;

//   Status = SystemTable->BootServices->AllocatePool(
//       EfiLoaderData, FileInfo->Size, (VOID**)(&ModuleMemory));
//   if (EFI_ERROR(Status)) {
//     return Status;
//   }

//   UINTN FileSize = FileInfo->Size;
//   Status = File->Read(File, &FileSize, (VOID *)ModuleMemory);
//   File->Close(File);

//   EFI_PHYSICAL_ADDRESS ModuleListMemory = 0;

//   Status = SystemTable->BootServices->AllocatePool(
//       EfiLoaderData, sizeof(MODULE_LOAD) * MAX_MODULE_COUNT, (VOID**)(&ModuleListMemory));
//   if (EFI_ERROR(Status)) {
//     return Status;
//   }

//   BOOLEAN ReadingFile = TRUE;
//   CHAR8* FilePointer = (CHAR8*)ModuleMemory;
//   while (ReadingFile)
//   {
//     #define CHECK_FILE_POINTER if (*FilePointer == 0) { ReadingFile = FALSE; break; }
//     CHAR8* ModuleType;
//     CHAR8* ModulePath;

//     while (*FilePointer != '[') { CHECK_FILE_POINTER FilePointer++; }
//     if (*FilePointer == '[')
//     {
//       FilePointer++;
//     }
//     else {
//       ReadingFile = FALSE;
//       break;
//     }
//     while (*FilePointer == ' ') { CHECK_FILE_POINTER FilePointer++; }

//     ModuleType = FilePointer;
//     while (*FilePointer != ' ' && *FilePointer != ']') { CHECK_FILE_POINTER FilePointer++; }

//     if (*FilePointer == ']')
//     {
//       *FilePointer = 0;
//       FilePointer++;
//     }
//     else {
//       *FilePointer = 0;
//       FilePointer++;
//       while (*FilePointer == ' ') { CHECK_FILE_POINTER FilePointer++; }
//       if (*FilePointer != ']')
//       {
//         ReadingFile = FALSE;
//         break;
//       }
//     }

//     while (*FilePointer == ' ') { CHECK_FILE_POINTER FilePointer++; }
//     ModulePath = FilePointer;

//     while (*FilePointer != ' ' && *FilePointer != '\n') { CHECK_FILE_POINTER FilePointer++; }
//     *FilePointer = 0;
//     FilePointer++;

//     PRINT(ConvertString(SystemTable, ModulePath));
//     PRINT(ConvertString(SystemTable, ModuleType));
//     MODULE_LOAD module = MODULE_ENTRY(ConvertString(SystemTable, ModulePath), ConvertString(SystemTable, ModulePath), GetModuleType(ModuleType));
//     ((MODULE_LOAD*)ModuleListMemory)[*ModuleCount] = module;
//     (*ModuleCount)++;
    
    
//   }

//   SystemTable->BootServices->FreePool(FileInfo);

//   return ModuleListMemory;
// }
EFI_STATUS
GetModulesToLoad(
  IN  EFI_SYSTEM_TABLE *SystemTable, 
  IN  EFI_FILE_PROTOCOL *Root, 
  OUT UINTN* ModuleCount,
  OUT MODULE_LOAD** Modules)
{
  EFI_STATUS Status;
  EFI_FILE_PROTOCOL *File = NULL;
  EFI_FILE_INFO *FileInfo = NULL;
  CHAR8* FileBuffer = NULL;
  MODULE_LOAD* ModuleList = NULL;
  
  // Initialize outputs
  *ModuleCount = 0;
  *Modules = NULL;

  // Open the modules.conf file
  Status = Root->Open(Root, &File, L"\\modules.conf", EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  
  // Get file info to determine size
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

  // Allocate module list
  Status = SystemTable->BootServices->AllocatePool(
      EfiLoaderData, sizeof(MODULE_LOAD) * MAX_MODULE_COUNT, (VOID**)&ModuleList);
  if (EFI_ERROR(Status)) {
    SystemTable->BootServices->FreePool(FileBuffer);
    return Status;
  }

  // Parse the file line by line
  CHAR8* FilePointer = FileBuffer;
  UINTN Count = 0;
  
  while (Count < MAX_MODULE_COUNT && *FilePointer != '\0') {
    // Skip leading whitespace and empty lines
    while (*FilePointer == ' ' || *FilePointer == '\t' || *FilePointer == '\n' || *FilePointer == '\r') {
      if (*FilePointer == '\0') break;
      FilePointer++;
    }
    
    if (*FilePointer == '\0') break;
    
    // Check for comment line (starting with #)
    if (*FilePointer == '#') {
      // Skip to end of line
      while (*FilePointer != '\n' && *FilePointer != '\0') FilePointer++;
      continue;
    }
    
    // Look for opening bracket
    if (*FilePointer != '[') {
      // Skip to next line if not a valid module entry
      while (*FilePointer != '\n' && *FilePointer != '\0') FilePointer++;
      continue;
    }
    
    FilePointer++; // Skip '['
    if (*FilePointer == '\0') break;
    
    // Skip spaces after '['
    while (*FilePointer == ' ' || *FilePointer == '\t') {
      if (*FilePointer == '\0') break;
      FilePointer++;
    }
    
    if (*FilePointer == '\0') break;
    
    // Get module type (text inside brackets)
    CHAR8* ModuleTypeStart = FilePointer;
    
    // Find closing bracket
    while (*FilePointer != ']' && *FilePointer != '\n' && *FilePointer != '\0') {
      FilePointer++;
    }
    
    if (*FilePointer != ']') {
      // Invalid format, skip to next line
      while (*FilePointer != '\n' && *FilePointer != '\0') FilePointer++;
      continue;
    }
    
    // Null-terminate module type
    *FilePointer = '\0';  // Replace ']' with null terminator
    FilePointer++; // Move past the null terminator
    
    // Skip whitespace after ']'
    while (*FilePointer == ' ' || *FilePointer == '\t') {
      if (*FilePointer == '\0') break;
      FilePointer++;
    }
    
    if (*FilePointer == '\0' || *FilePointer == '\n' || *FilePointer == '\r') {
      // No path specified, skip this entry
      // Move to next line
      while (*FilePointer == '\n' || *FilePointer == '\r') FilePointer++;
      continue;
    }
    
    // Get module path (rest of the line)
    CHAR8* ModulePathStart = FilePointer;
    
    // Find end of line
    while (*FilePointer != '\n' && *FilePointer != '\r' && *FilePointer != '\0') {
      FilePointer++;
    }
    
    // Null-terminate module path
    CHAR8 savedChar = *FilePointer;  // Save newline or null
    *FilePointer = '\0';
    
    // Trim trailing whitespace from path
    CHAR8* PathEnd = FilePointer - 1;
    while (PathEnd >= ModulePathStart && (*PathEnd == ' ' || *PathEnd == '\t')) {
      *PathEnd = '\0';
      PathEnd--;
    }
    
    // Convert strings
    CHAR16* ModulePathWide = ConvertString(SystemTable, ModulePathStart);
    CHAR16* ModuleTypeWide = ConvertString(SystemTable, ModuleTypeStart);

    if (ModulePathWide && ModuleTypeWide) {
      // Create module entry - FIXED: Use ModulePathWide for path, ModuleTypeWide for type
      MODULE_LOAD module = MODULE_ENTRY(ModulePathWide, ModuleTypeWide, GetModuleType(ModuleTypeStart));
      ModuleList[Count] = module;
      Count++;
    }
    
    // Restore saved character and move to next line
    *FilePointer = savedChar;
    
    // Skip to next line
    while (*FilePointer == '\n' || *FilePointer == '\r') {
      if (*FilePointer == '\0') break;
      FilePointer++;
    }
  }

  // Free file buffer
  SystemTable->BootServices->FreePool(FileBuffer);
  
  // Set outputs
  *ModuleCount = Count;
  *Modules = ModuleList;
  
  return EFI_SUCCESS;
}

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

  UINTN ModuleCount;
  MODULE_LOAD *Modules;
  Status = GetModulesToLoad(SystemTable, Root, &ModuleCount, &Modules);


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
