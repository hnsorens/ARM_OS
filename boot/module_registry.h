#ifndef REGISTRY_H
#define REGISTRY_H


#include "stdint.h"
#include "stddef.h"

#define NULL ((void*)0)
#define MAX_STR_LEN 32
#define SUBMAP_INITIAL_CAPACITY 16

/* Data for a single module instance */
typedef struct {
    char name_str[MAX_STR_LEN];
    uint64_t name_hash;
    void* vtable_ptr;
    uint32_t occupied;
} instance_entry_t;

/* Data for a module category (Type) */
typedef struct {
    char type_str[MAX_STR_LEN];
    uint64_t type_hash;
    instance_entry_t* instance_table;
    size_t instance_capacity;
    uint32_t occupied;
} type_entry_t;

/* The Master Control Structure */
typedef struct {
    type_entry_t* type_table;
    size_t type_capacity;
    size_t type_count;
    
    uint8_t* pool_ptr;      /* Where we carve out new Sub-Maps */
    size_t pool_remaining;
} registry_t;

typedef struct {
    const char* type;
    const char* name;
    void* vtable_ptr;
} module_meta_t;

void registry_init(void* block, size_t block_size, size_t max_expected_types);
int registry_put(module_meta_t meta);
void* registry_get(const char* type, const char* name);
void* registry_get_any(const char* type);

#endif
