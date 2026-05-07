#include "kernel_loader.h"

#include "elf_loader.h"
#include "filesystem.h"

static VOID *Memcpy(VOID *Dest, CONST VOID *Src, UINTN N)
{
	UINT8 *D = Dest;
	CONST UINT8 *S = Src;
	while (N--)
		*D++ = *S++;
	return Dest;
}

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

static CHAR8 *Strchr(CONST CHAR8 *S, CHAR8 C)
{
	while (*S != C) {
		if (!*S)
			return 0;
		S++;
	}
	return (CHAR8 *)S;
}

static INT32 Strlen(CONST CHAR8 *S)
{
	UINT32 I = 0;
	while (S[I])
		I++;
	return I;
}

static char *last_token = 0;

char *Strtok(char *str, const char *delim)
{
	// If str is NULL, continue from the last saved position
	if (str == 0) {
		str = last_token;
	}

	// If we've reached the end of the string, return NULL
	if (str == 0 || *str == '\0') {
		return 0;
	}

	// 1. Skip leading delimiters
	while (*str) {
		int is_delim = 0;
		for (int i = 0; delim[i]; i++) {
			if (*str == delim[i]) {
				is_delim = 1;
				break;
			}
		}
		if (!is_delim)
			break;
		str++;
	}

	// If we reached the end while skipping delimiters
	if (*str == '\0') {
		last_token = 0;
		return 0;
	}

	// 2. Find the end of the current token
	char *token_start = str;
	while (*str) {
		int is_delim = 0;
		for (int i = 0; delim[i]; i++) {
			if (*str == delim[i]) {
				is_delim = 1;
				break;
			}
		}
		if (is_delim) {
			*str = '\0'; // Terminate the token
			last_token = str + 1; // Save the next position
			return token_start;
		}
		str++;
	}

	// If we reached the end of the string, this is the last token
	last_token = 0;
	return token_start;
}

CHAR8 *Trim(CHAR8 *S)
{
	CHAR8 *Back;
	while (*S == ' ' || *S == '\t' || *S == '\r')
		S++;
	if (*S == 0)
		return S;

	Back = S + Strlen(S) - 1;
	while (Back > S && (*Back == ' ' || *Back == '\t' || *Back == '\r' ||
			    *Back == '\n'))
		Back--;
	*(Back + 1) = 0;
	return S;
}

UINTN Offsethehe = 0;

EFI_STATUS
Load_Module(EFI_SYSTEM_TABLE *SystemTable, EFI_HANDLE ImageHandle,
	    EFI_FILE_PROTOCOL *Root, CHAR8 *Name, PAGE_TABLE_T *PageTable,
	    EFI_VIRTUAL_ADDRESS *Entry)
{
	Boot_Log("Loading a module\n", 17);
	CHAR16 NameBuffer[256];

	NameBuffer[0] = '\\';

	UINTN NameLength = Strlen(Name);
	for (UINTN I = 0; I < NameLength; ++I) {
		NameBuffer[I + 1] = Name[I];
	}

	NameBuffer[NameLength + 1] = '.';
	NameBuffer[NameLength + 2] = 'e';
	NameBuffer[NameLength + 3] = 'l';
	NameBuffer[NameLength + 4] = 'f';
	NameBuffer[NameLength + 5] = '\0';

	CHAR8 *ModuleBuffer = 0;
	ReadFile(NameBuffer, Root, SystemTable, ImageHandle, &ModuleBuffer);

	Load_Elf(SystemTable, ModuleBuffer, Offsethehe, PageTable, Entry);
	Offsethehe += (4096 * 10); // FOR TESTING FIX LATER SO ITS ACTUAL VALUE

	//SystemTable->BootServices->FreePool(ModuleBuffer);

	return EFI_SUCCESS;
}

static VOID Parse_Config(EFI_SYSTEM_TABLE *SystemTable, EFI_HANDLE ImageHandle,
			 EFI_FILE_PROTOCOL *Root, PAGE_TABLE_T *UpperPageTable,
			 CHAR8 *FileBuffer)
{
	CHAR8 *Line = Strtok(FileBuffer, "\n");
	INT32 InSection = 0;

	while (Line != 0) {
		// Skip comments and empty lines
		if (Line[0] == ';' || Line[0] == '#' || Line[0] == '\r' ||
		    Line[0] == '\n' || Line[0] == '\0') {
			Line = Strtok(0, "\n");
			continue;
		}

		// Check for [Modules] section
		if (Line[0] == '[') {
			if (Strncmp(Line, "[Modules]", 9) == 0) {
				InSection = 1;
			} else {
				InSection = 0;
			}
		}

		// Extract Module and Status
		else if (InSection) {
			CHAR8 *EqualSign = Strchr(Line, '=');
			if (EqualSign) {
				*EqualSign = '\0'; // Split the string
				CHAR8 *Name = Trim(Line);
				CHAR8 *Status = Trim(EqualSign + 1);

				if (Status[0] == 'Y' || Status[0] == 'y') {
					EFI_VIRTUAL_ADDRESS Entry;
					Load_Module(SystemTable, ImageHandle,
						    Root, Name, UpperPageTable,
						    &Entry);
				}
			}
		}
		Line = Strtok(0, "\n");
	}
}

EFI_STATUS
Load_Kernel(EFI_SYSTEM_TABLE *SystemTable, EFI_HANDLE ImageHandle,
	    EFI_FILE_PROTOCOL *Root, CHAR16 *ConfigurationFileName,
	    EFI_VIRTUAL_ADDRESS *Entry, PAGE_TABLE_T *UpperPageTable)
{
	EFI_STATUS Status;

	Load_Module(SystemTable, ImageHandle, Root, "Kernel", UpperPageTable,
		    Entry);

	Boot_Log("Loaded kernel core\n", 19);

	CHAR8 *ConfigurationBuffer;
	ReadFile(ConfigurationFileName, Root, SystemTable, ImageHandle,
		 &ConfigurationBuffer);

	Boot_Log("Read kernel configuration file\n", 31);

	Parse_Config(SystemTable, ImageHandle, Root, UpperPageTable,
		     ConfigurationBuffer);

	return EFI_SUCCESS;
}
