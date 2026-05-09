#include "module_registry.h"

/* --- Internal Helpers --- */

#include "logging/serial.h"

REGISTRY Reg;
INSTANCE_ENTRY *Head = 0;

static void *Memset(void *S, INT32 C, UINTN N)
{
	unsigned char *P = (unsigned char *)S;
	while (N--)
		*P++ = (unsigned char)C;
	return S;
}

static INT32 Strcmp(const char *S1, const char *S2)
{
	while (*S1 && (*S1 == *S2)) {
		S1++;
		S2++;
	}
	return *(unsigned char *)S1 - *(unsigned char *)S2;
}

static void Strlcpy(char *Dest, const char *Src, UINTN N)
{
	UINTN I;
	for (I = 0; I < N - 1 && Src[I] != '\0'; I++)
		Dest[I] = Src[I];
	Dest[I] = '\0';
}

static UINT64 Hash(const char *Str)
{
	UINT64 HashValue = 0xcbf29ce484222325;
	while (*Str) {
		HashValue ^= (uint8_t)*Str++;
		HashValue *= 0x100000001b3;
	}
	return HashValue;
}

/* --- Core Logic --- */
EFI_STATUS RegistryInit(void *Block, UINTN BlockSize, UINTN MaxExpectedTypes)
{
	Memset(Block, 0, BlockSize);

	// 1. Allocate the Master Type Table at the very start of the block
	Reg.TypeTable = (TYPE_ENTRY *)Block;
	Reg.TypeCapacity = MaxExpectedTypes;
	Reg.TypeCount = 0;

	// 2. Set up the pool for Sub-Maps (offset by the size of the Master Table)
	UINTN TableSize = sizeof(TYPE_ENTRY) * MaxExpectedTypes;
	Reg.PoolPtr = (uint8_t *)Block + TableSize;
	Reg.PoolRemaining = BlockSize - TableSize;

	if (BlockSize < TableSize) {
		return EFI_OUT_OF_RESOURCES;
	}
	return EFI_SUCCESS;
}

EFI_STATUS RegistryPut(MODULE_META Meta)
{
	UINT64 THash = Hash(Meta.Type);

	// 1. Find or Create Type in the Master Map using Linear Probing
	UINT32 TIdx = THash % Reg.TypeCapacity;
	UINT32 TStart = TIdx;

	while (Reg.TypeTable[TIdx].Occupied) {
		if (Reg.TypeTable[TIdx].TypeHash == THash)
			break;
		TIdx = (TIdx + 1) % Reg.TypeCapacity;
		if (TIdx == TStart)
			return -1; // Master Map is full!
	}

	TYPE_ENTRY *TypeBucket = &Reg.TypeTable[TIdx];

	if (!TypeBucket->Occupied) {
		// Initialize new Sub-Map
		UINTN SubmapSz =
			sizeof(INSTANCE_ENTRY) * SUBMAP_INITIAL_CAPACITY;
		if (Reg.PoolRemaining < SubmapSz)
			return -2; // Out of memory

		TypeBucket->TypeHash = THash;
		Strlcpy(TypeBucket->TypeString, Meta.Type, MAX_STR_LEN);
		TypeBucket->InstanceTable = (INSTANCE_ENTRY *)Reg.PoolPtr;
		TypeBucket->InstanceCapacity = SUBMAP_INITIAL_CAPACITY;
		TypeBucket->Occupied = 1;

		Reg.PoolPtr += SubmapSz;
		Reg.PoolRemaining -= SubmapSz;
		Reg.TypeCount++;
	}

	// 2. Insert into the Instance Sub-Map using Linear Probing
	UINT64 NHash = Hash(Meta.Name);
	UINT32 NIdx = NHash % TypeBucket->InstanceCapacity;
	UINT32 NStart = NIdx;

	while (TypeBucket->InstanceTable[NIdx].Occupied) {
		// Handle duplicate put
		if (TypeBucket->InstanceTable[NIdx].NameHash == NHash)
			break;

		NIdx = (NIdx + 1) % TypeBucket->InstanceCapacity;
		if (NIdx == NStart)
			return -3; // Sub-Map is full!
	}

	INSTANCE_ENTRY *Entry = &TypeBucket->InstanceTable[NIdx];
	Entry->NameHash = NHash;
	Entry->VTablePtr = Meta.VTablePtr;
	Entry->VTableSize = Meta.VTableSize;
	Strlcpy(Entry->NameString, Meta.Name, MAX_STR_LEN);
	Entry->Occupied = 1;
	Entry->Entry = Meta.Entry;
	Entry->DependenciesHead = 0;
	Entry->Initialized = FALSE;

	if (Head) {
		Entry->Next = Head;
	} else {
		Entry->Next = 0;
	}

	Head = Entry;

	return 0;
}

static void RegistryGetInstanceEntry(const char *Type, const char *Name,
				     INSTANCE_ENTRY **Ptr)
{
	UINT64 THash = Hash(Type);
	UINT32 TIdx = THash % Reg.TypeCapacity;
	UINT32 TStart = TIdx;

	while (Reg.TypeTable[TIdx].Occupied) {
		if (Reg.TypeTable[TIdx].TypeHash == THash) {
			TYPE_ENTRY *Sub = &Reg.TypeTable[TIdx];
			UINT64 NHash = Hash(Name);
			UINT32 NIdx = NHash % Sub->InstanceCapacity;
			UINT32 NStart = NIdx;

			while (Sub->InstanceTable[NIdx].Occupied) {
				if (Sub->InstanceTable[NIdx].NameHash ==
				    NHash) {
					*Ptr = &Sub->InstanceTable[NIdx];
				}
				NIdx = (NIdx + 1) % Sub->InstanceCapacity;
				if (NIdx == NStart)
					break;
			}
			return;
		}
		TIdx = (TIdx + 1) % Reg.TypeCapacity;
		if (TIdx == TStart)
			break;
	}
}

void RegistryGet(const char *Type, const char *Name, void **Ptr, UINTN *Size)
{
	UINT64 THash = Hash(Type);
	UINT32 TIdx = THash % Reg.TypeCapacity;
	UINT32 TStart = TIdx;

	while (Reg.TypeTable[TIdx].Occupied) {
		if (Reg.TypeTable[TIdx].TypeHash == THash) {
			TYPE_ENTRY *Sub = &Reg.TypeTable[TIdx];
			UINT64 NHash = Hash(Name);
			UINT32 NIdx = NHash % Sub->InstanceCapacity;
			UINT32 NStart = NIdx;

			while (Sub->InstanceTable[NIdx].Occupied) {
				if (Sub->InstanceTable[NIdx].NameHash ==
				    NHash) {
					*Ptr = Sub->InstanceTable[NIdx]
						       .VTablePtr;
					*Size = Sub->InstanceTable[NIdx]
							.VTableSize;
				}
				NIdx = (NIdx + 1) % Sub->InstanceCapacity;
				if (NIdx == NStart)
					break;
			}
			return;
		}
		TIdx = (TIdx + 1) % Reg.TypeCapacity;
		if (TIdx == TStart)
			break;
	}
}

void RegistryResolveName(const char *Type, const char **Name)
{
	UINT64 THash = Hash(Type);
	UINT32 TIdx = THash % Reg.TypeCapacity;
	UINT32 TStart = TIdx;

	while (Reg.TypeTable[TIdx].Occupied) {
		if (Reg.TypeTable[TIdx].TypeHash == THash) {
			TYPE_ENTRY *Sub = &Reg.TypeTable[TIdx];

			// Iterate through the sub-map to find the first occupied instance
			for (UINTN I = 0; I < Sub->InstanceCapacity; I++) {
				if (Sub->InstanceTable[I].Occupied) {
					// Assign the pointer to the internal NameString
					*Name = Sub->InstanceTable[I].NameString;
					return;
				}
			}
		}

		TIdx = (TIdx + 1) % Reg.TypeCapacity;
		if (TIdx == TStart)
			break;
	}

	// If not found, ensure the pointer is set to NULL
	if (Name) {
		*Name = 0;
	}
}

EFI_STATUS RegistryPutDependency(CONST CHAR8 *TypeString,
				 CONST CHAR8 *NameString,
				 CONST CHAR8 *DepTypeString,
				 CONST CHAR8 *DepNameString)
{
	if (Reg.PoolRemaining < sizeof(MODULE_DEPENDENCY))
		return EFI_OUT_OF_RESOURCES;

	MODULE_DEPENDENCY *Dependency = (MODULE_DEPENDENCY *)Reg.PoolPtr;
	Reg.PoolPtr += sizeof(MODULE_DEPENDENCY);
	Reg.PoolRemaining -= sizeof(MODULE_DEPENDENCY);

	INSTANCE_ENTRY *Instance = 0;
	INSTANCE_ENTRY *DependencyInstance = 0;
	RegistryGetInstanceEntry(TypeString, NameString, &Instance);
	RegistryGetInstanceEntry(DepTypeString, DepNameString,
				 &DependencyInstance);

	Dependency->Entry = DependencyInstance;
	Dependency->Next = Instance->DependenciesHead;
	Instance->DependenciesHead = Dependency;

	return EFI_SUCCESS;
}

#define MAX_MODULE_DEPTH 10

EFI_STATUS InitializeModule(VOID *Data, INSTANCE_ENTRY *Instance, UINTN Depth)
{
	if (Depth > MAX_MODULE_DEPTH)
		return EFI_INVALID_PARAMETER;
	if (Instance->Initialized)
		return EFI_SUCCESS;
	EFI_STATUS Status;
	MODULE_DEPENDENCY *Current = Instance->DependenciesHead;
	while (Current) {
		Status = InitializeModule(Data, Current->Entry, Depth + 1);
		if (EFI_ERROR(Status)) {
			Fail_Log("Initialized module\n", 19);
			return Status;
		}
		Ok_Log("Initialized module\n", 19);
		Current = Current->Next;
	}

	if (!Instance->Initialized) {
		Instance->Entry(Data);
		Instance->Initialized = TRUE;
	}

	return EFI_SUCCESS;
}

EFI_STATUS InitializeModules(VOID *Data)
{
	INSTANCE_ENTRY *Current = Head;
	EFI_STATUS Status;
	while (Current) {
		Status = InitializeModule(Data, Current, 0);
		if (EFI_ERROR(Status)) {
			Fail_Log("Initialized module\n", 19);
			return Status;
		}
		Ok_Log("Initialized module\n", 19);
		Current = Current->Next;
	}

	return EFI_SUCCESS;
}
