#ifndef REGISTRY_H
#define REGISTRY_H

#include <efi.h>
#include <efilib.h>

#define NULL ((void*)0)
#define MAX_STR_LEN 32
#define SUBMAP_INITIAL_CAPACITY 16

typedef struct MODULE_DEPENDENCY
{
    struct INSTANCE_ENTRY *Entry;
    struct MODULE_DEPENDENCY *Next;
} MODULE_DEPENDENCY;

typedef struct TEST_ENTRY {
    const char *TestName;
    UINT32 (*Test)(VOID);
} TEST_ENTRY;

/* Data for a single module instance */
typedef struct INSTANCE_ENTRY
{
    CHAR8 NameString[MAX_STR_LEN];
    UINT64 NameHash;
    VOID *VTablePtr;
    UINT64 VTableSize;
    MODULE_DEPENDENCY *DependenciesHead;
    VOID (*Entry)(VOID *);
    BOOLEAN Initialized;
    struct INSTANCE_ENTRY *Next;
    UINT32 Occupied;
    TEST_ENTRY *Tests;
    UINTN TestCount;
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
    VOID (*Entry)(VOID *);
    TEST_ENTRY *TestStart;
    UINTN TestCount;

} MODULE_META;

EFI_STATUS RegistryInit(VOID *Block, UINTN BlockSize, UINTN MaxExpectedTypes);
EFI_STATUS RegistryPut(MODULE_META Meta);
VOID RegistryGet(CONST CHAR8 *Type, CONST CHAR8 *Name, VOID** Ptr, UINT64 *Size);
VOID RegistryResolveName(CONST CHAR8 *Type, CONST CHAR8 **Name);
EFI_STATUS RegistryPutDependency(CONST CHAR8 *TypeString, CONST CHAR8 *NameString, CONST CHAR8 *DepTypeString, CONST CHAR8 *DepNameString);
EFI_STATUS InitializeModules(VOID *Data);

#endif
