#ifndef REGISTRY_H
#define REGISTRY_H

#include <efi.h>
#include <efilib.h>

#define NULL ((void*)0)
#define MAX_STR_LEN 32
#define SUBMAP_INITIAL_CAPACITY 16

/* Data for a single module instance */
typedef struct INSTANCE_ENTRY
{
    CHAR8 NameString[MAX_STR_LEN];
    UINT64 NameHash;
    VOID *VTablePtr;
    UINT64 VTableSize;
    UINT32 Occupied;
} INSTANCE_ENTRY;

/* Data for a module category (Type) */
typedef struct TYPE_ENTRY
{
    CHAR8 TypeString[MAX_STR_LEN];
    UINT64 TypeHash;
    INSTANCE_ENTRY *InstanceTable;
    UINTN InstanceCapacity;
    UINT32 Occupied;
} TYPE_ENTRY;

/* The Master Control Structure */
typedef struct REGISTRY
{
    TYPE_ENTRY *TypeTable;
    UINTN TypeCapacity;
    UINTN TypeCount;
    UINT8 *PoolPtr;
    UINTN PoolRemaining;
} REGISTRY;

typedef struct MODULE_META 
{
    CONST CHAR8 *Type;
    CONST CHAR8 *Name;
    VOID *VTablePtr;
    UINT64 VTableSize;
} MODULE_META;

VOID RegistryInit(VOID *Block, UINTN BlockSize, UINTN MaxExpectedTypes);
INT32 RegistryPut(MODULE_META Meta);
VOID RegistryGet(CONST CHAR8 *Type, CONST CHAR8 *Name, VOID** Ptr, UINT64 *Size);
VOID RegistryGetAny(CONST CHAR8 *type, VOID** ptr, UINT64 *Size);

#endif
