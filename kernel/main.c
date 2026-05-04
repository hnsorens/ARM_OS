#include "../modules/serial_debug/serial.h"

#include "module_registery.h"
#include "memory.h"
#include "../boot/bootinfo.h"

extern Metadata __vtable_all_start[];
extern Metadata __vtable_all_end[];

int global_variable = 0;

int _start(BootInfoStruct *BootInfo)
{
	serial_debug_serial_printf("Metadata Name %lx\n",
				   BootInfo->memoryMapSize);
	setup_kernel_allocator(BootInfo);
	serial_debug_serial_printf("Allocated memory\n");
	build_module_registry(BootInfo);
	serial_debug_serial_printf("Setup registry\n");
	while (1)
		;
}
