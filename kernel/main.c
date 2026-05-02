#include "../modules/serial_debug/serial.h"

#include "module_registery.h"
#include "../boot/bootinfo.h"

extern Metadata __vtable_all_start[];
extern Metadata __vtable_all_end[];

int _start(BootInfoStruct *BootInfo)
{
	//serial_debug_serial_printf("Jump To Kernel Was Successful!\n");
	serial_debug_serial_printf("Metadata Name %s\n",
				   __vtable_all_start[0].identifier);
	build_module_registry();
	while (1)
		;
}
