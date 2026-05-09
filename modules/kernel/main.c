
#include "../../boot/uefi/bootinfo.h"
#include "../../include/api/serial_debug.h"

#include "../modules.h"

IMPORT_MODULE_ANY(Serial, serial)

EXPORT_MODULE(Serial, ModuleName, {})

int global_variable = 0;

int _start(BootInfoStruct *BootInfo)
{
	//serial_debug_serial_printf("TEST STARTING\n");
	serial.printf("FUNCTION CALL FROM VTABLE\n");
	//serial_debug_serial_printf("TEST SUCCESS\n");
}
