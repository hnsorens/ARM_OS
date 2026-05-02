#include "module_registery.h"

#include "../modules/serial_debug/serial.h"
#include "module_info.h"
#include "memory.h"

extern Metadata __vtable_all_start[];
extern Metadata __vtable_all_end[];



inline char get_module_type(unsigned long identifier)
{
    return identifier & 0b11111111; // 256 differnt modules types
}

char verify_vtable_function(void* function_ptr)
{
    // verify valid function by checking to make sure it points to BTI
    // also make sure that the pointer is inside of the module
    return 1;
}

void allocate_module_registry(BootInfoStruct *bootInfo)
{
    void* top_level = kernel_alloc(bootInfo, 1);
}

void add_vtable(unsigned long id, void* vtable, unsigned long vtable_size)
{

}

void build_module_registry()
{
	unsigned long vtableCount = (__vtable_all_end - __vtable_all_start);
	serial_debug_serial_printf("Vtable Count: %d\n", vtableCount);

    for (int vtable_index = 0; vtable_index < vtableCount; ++vtable_index)
    {
        Metadata *vtable_meta = &__vtable_all_start[vtable_index];
        char module_type = get_module_type(vtable_meta->identifier);
        char functions_valid = 1;
        void** vtable_functions = vtable_meta->vtable;

        // Validate all functions to make sure they are valid function pointers in the given module
        for (int func_index = 0; func_index < module_func_count[module_type]; ++func_index)
        {
            functions_valid &= verify_vtable_function(vtable_functions[func_index]);
        }

        if (!functions_valid)
        {
            // reject the module
            continue;
        }

        // register the module into the registry
        add_vtable(vtable_meta->identifier, vtable_meta->vtable, module_func_count[module_type]);
    }
}
