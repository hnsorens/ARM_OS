#include "module_registry.h"

/* --- Internal Helpers --- */

registry_t reg;

static void *k_memset(void *s, int c, size_t n)
{
	unsigned char *p = (unsigned char *)s;
	while (n--)
		*p++ = (unsigned char)c;
	return s;
}

static int k_strcmp(const char *s1, const char *s2)
{
	while (*s1 && (*s1 == *s2)) {
		s1++;
		s2++;
	}
	return *(unsigned char *)s1 - *(unsigned char *)s2;
}

static void k_strlcpy(char *dest, const char *src, size_t n)
{
	size_t i;
	for (i = 0; i < n - 1 && src[i] != '\0'; i++)
		dest[i] = src[i];
	dest[i] = '\0';
}

static uint64_t k_hash(const char *str)
{
	uint64_t hash = 0xcbf29ce484222325;
	while (*str) {
		hash ^= (uint8_t)*str++;
		hash *= 0x100000001b3;
	}
	return hash;
}

/* --- Core Logic --- */

void registry_init(void *block, size_t block_size, size_t max_expected_types)
{
	k_memset(block, 0, block_size);

	// 1. Allocate the Master Type Table at the very start of the block
	reg.type_table = (type_entry_t *)block;
	reg.type_capacity = max_expected_types;
	reg.type_count = 0;

	// 2. Set up the pool for Sub-Maps (offset by the size of the Master Table)
	size_t table_size = sizeof(type_entry_t) * max_expected_types;
	reg.pool_ptr = (uint8_t *)block + table_size;
	reg.pool_remaining = block_size - table_size;
}

int registry_put(module_meta_t meta)
{
	uint64_t t_hash = k_hash(meta.type);

	// 1. Find or Create Type in the Master Map using Linear Probing
	uint32_t t_idx = t_hash % reg.type_capacity;
	uint32_t t_start = t_idx;

	while (reg.type_table[t_idx].occupied) {
		if (reg.type_table[t_idx].type_hash == t_hash)
			break;
		t_idx = (t_idx + 1) % reg.type_capacity;
		if (t_idx == t_start)
			return -1; // Master Map is full!
	}

	type_entry_t *type_bucket = &reg.type_table[t_idx];

	if (!type_bucket->occupied) {
		// Initialize new Sub-Map
		size_t submap_sz =
			sizeof(instance_entry_t) * SUBMAP_INITIAL_CAPACITY;
		if (reg.pool_remaining < submap_sz)
			return -2; // Out of memory

		type_bucket->type_hash = t_hash;
		k_strlcpy(type_bucket->type_str, meta.type, MAX_STR_LEN);
		type_bucket->instance_table = (instance_entry_t *)reg.pool_ptr;
		type_bucket->instance_capacity = SUBMAP_INITIAL_CAPACITY;
		type_bucket->occupied = 1;

		reg.pool_ptr += submap_sz;
		reg.pool_remaining -= submap_sz;
		reg.type_count++;
	}

	// 2. Insert into the Instance Sub-Map using Linear Probing
	uint64_t n_hash = k_hash(meta.name);
	uint32_t n_idx = n_hash % type_bucket->instance_capacity;
	uint32_t n_start = n_idx;

	while (type_bucket->instance_table[n_idx].occupied) {
		// Handle duplicate put (optional: update vtable or error)
		if (type_bucket->instance_table[n_idx].name_hash == n_hash)
			break;

		n_idx = (n_idx + 1) % type_bucket->instance_capacity;
		if (n_idx == n_start)
			return -3; // Sub-Map is full!
	}

	instance_entry_t *entry = &type_bucket->instance_table[n_idx];
	entry->name_hash = n_hash;
	entry->vtable_ptr = meta.vtable_ptr;
	k_strlcpy(entry->name_str, meta.name, MAX_STR_LEN);
	entry->occupied = 1;

	return 0;
}

void *registry_get(const char *type, const char *name)
{
	uint64_t t_hash = k_hash(type);
	uint32_t t_idx = t_hash % reg.type_capacity;
	uint32_t t_start = t_idx;

	while (reg.type_table[t_idx].occupied) {
		if (reg.type_table[t_idx].type_hash == t_hash) {
			type_entry_t *sub = &reg.type_table[t_idx];
			uint64_t n_hash = k_hash(name);
			uint32_t n_idx = n_hash % sub->instance_capacity;
			uint32_t n_start = n_idx;

			while (sub->instance_table[n_idx].occupied) {
				if (sub->instance_table[n_idx].name_hash ==
				    n_hash) {
					return sub->instance_table[n_idx]
						.vtable_ptr;
				}
				n_idx = (n_idx + 1) % sub->instance_capacity;
				if (n_idx == n_start)
					break;
			}
			return NULL;
		}
		t_idx = (t_idx + 1) % reg.type_capacity;
		if (t_idx == t_start)
			break;
	}
	return NULL;
}

void *registry_get_any(const char *type)
{
	uint64_t t_hash = k_hash(type);
	uint32_t t_idx = t_hash % reg.type_capacity;
	uint32_t t_start = t_idx;

	while (reg.type_table[t_idx].occupied) {
		if (reg.type_table[t_idx].type_hash == t_hash) {
			type_entry_t *sub = &reg.type_table[t_idx];
			for (size_t i = 0; i < sub->instance_capacity; i++) {
				if (sub->instance_table[i].occupied)
					return sub->instance_table[i].vtable_ptr;
			}
		}
		t_idx = (t_idx + 1) % reg.type_capacity;
		if (t_idx == t_start)
			break;
	}
	return NULL;
}
