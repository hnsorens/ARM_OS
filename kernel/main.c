#include "../modules/serial_debug/serial.h"

#include "../boot/bootinfo.h"

__attribute__((section(".import.serial.first_serial_hehe"), used,
	       aligned(8))) volatile static const SerialDeviceInterface serial;

int global_variable = 0;

int _start(BootInfoStruct *BootInfo)
{
	//serial_debug_serial_printf("TEST STARTING\n");
	serial.printf("FUNCTION CALL FROM VTABLE\n");
	//serial_debug_serial_printf("TEST SUCCESS\n");
}
