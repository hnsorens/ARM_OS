#include "module_registery.h"

extern Metadata __vtable_all_start[];
extern Metadata __vtable_all_end[];

extern char module_func_count[];

unsigned long *vtable_storage_current = 0;
unsigned long *registry_top_level = 0;

typedef struct ModuleMetadataPageMetadata {
	unsigned long count;
	unsigned long reserved;
} ModuleMetadataPageMetadata;

inline char get_module_type(unsigned long identifier)
{
	return identifier & 0b11111111; // 256 differnt modules types
}

char verify_vtable_function(void *function_ptr)
{
	// verify valid function by checking to make sure it points to BTI
	// also make sure that the pointer is inside of the module
	return 1;
}

void allocate_module_registry(BootInfoStruct *bootInfo)
{
	registry_top_level = kernel_alloc_page();
	memset(registry_top_level, 0, 4096);

	serial_debug_serial_printf("Setup top level registry at %lx\n",
				   registry_top_level);
}

void *add_vtable(void *vtable, unsigned long vtable_func_count)
{
	serial_debug_serial_printf("Adding vtable\n");
	unsigned long size_in_bytes = vtable_func_count * 8;
	unsigned long current_ptr_val = (unsigned long)vtable_storage_current;

	// Check if we have enough room left in the current 4KB page
	// (4096 - (current_ptr_val % 4096)) is the remaining bytes in the page
	if (!vtable_storage_current ||
	    (4096 - (current_ptr_val % 4096)) < size_in_bytes) {
		vtable_storage_current = kernel_alloc_page();
		memset(vtable_storage_current, 0, 4096);
		serial_debug_serial_printf(
			"Allocated page for module vtables hehe %lx\n",
			vtable_storage_current);
	}

	void *vtable_ptr = vtable_storage_current;
	memcpy(vtable_storage_current, vtable, size_in_bytes);

	// Move the pointer forward by the actual bytes written
	vtable_storage_current =
		(unsigned long *)((unsigned char *)vtable_storage_current +
				  size_in_bytes);

	return vtable_ptr;
}

void register_module(Metadata *metadata)
{
	if (!registry_top_level[metadata->identifier]) {
		registry_top_level[metadata->identifier] =
			(unsigned long)kernel_alloc_page();
		serial_debug_serial_printf(
			"Allocated page for module metadata %lx\n",
			registry_top_level[metadata->identifier]);
		memset((void *)registry_top_level[metadata->identifier], 0,
		       4096);
	}

	Metadata *metadata_page =
		(Metadata *)registry_top_level[metadata->identifier];
	ModuleMetadataPageMetadata *meta =
		(ModuleMetadataPageMetadata *)metadata_page;

	meta->count++;

	if (meta->count == (4096 / sizeof(Metadata))) {
		// panic because there are too many modules
	}

	void *vtable_ptr = add_vtable(metadata->vtable,
				      module_func_count[metadata->identifier]);

	memcpy(&metadata_page[meta->count], metadata, sizeof(Metadata));
	metadata_page[meta->count].vtable = vtable_ptr;
}

void build_module_registry(BootInfoStruct *bootInfo)
{
	unsigned long vtableCount = (__vtable_all_end - __vtable_all_start);
	serial_debug_serial_printf("Vtable Count: %d\n", vtableCount);

	allocate_module_registry(bootInfo);

	for (int vtable_index = 0; vtable_index < vtableCount; ++vtable_index) {
		Metadata *vtable_meta = &__vtable_all_start[vtable_index];
		char module_type = vtable_meta->identifier;
		char functions_valid = 1;
		void **vtable_functions = vtable_meta->vtable;

		// Validate all functions to make sure they are valid function pointers in the given module
		for (int func_index = 0;
		     func_index < module_func_count[module_type];
		     ++func_index) {
			functions_valid &= verify_vtable_function(
				vtable_functions[func_index]);
		}

		if (!functions_valid) {
			// reject the module
			continue;
		}

		// register the module into the registry
		register_module(vtable_meta);
	}
}
