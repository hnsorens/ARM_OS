#include "module_registry.h"

/* --- Internal Helpers --- */

REGISTRY Reg;

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

void RegistryInit(void *Block, UINTN BlockSize, UINTN MaxExpectedTypes)
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
}

INT32 RegistryPut(MODULE_META Meta)
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

	return 0;
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

void RegistryGetAny(const char *Type, void **Ptr, UINTN *Size)
{
	UINT64 THash = Hash(Type);
	UINT32 TIdx = THash % Reg.TypeCapacity;
	UINT32 TStart = TIdx;

	while (Reg.TypeTable[TIdx].Occupied) {
		if (Reg.TypeTable[TIdx].TypeHash == THash) {
			TYPE_ENTRY *Sub = &Reg.TypeTable[TIdx];
			for (UINTN I = 0; I < Sub->InstanceCapacity; I++) {
				if (Sub->InstanceTable[I].Occupied) {
					*Ptr = Sub->InstanceTable[I].VTablePtr;
					*Size = Sub->InstanceTable[I].VTableSize;
					return;
				}
			}
		}
		TIdx = (TIdx + 1) % Reg.TypeCapacity;
		if (TIdx == TStart)
			break;
	}
}
