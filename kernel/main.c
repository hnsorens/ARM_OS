#include "../modules/serial_debug/serial.h"

#include "../boot/bootinfo.h"

__attribute__((section(".import.serial.first_serial_hehe"), used,
	       aligned(8))) volatile static const SerialDeviceInterface serial;

int global_variable = 0;

int _start(BootInfoStruct *BootInfo)
{
	serial_debug_serial_printf("TEST %lx\n", &serial);
	serial_debug_serial_printf("ASD %lx\n", serial);
	serial_debug_serial_printf("%lx\n", &serial.printf);
	serial_debug_serial_printf("HEHE %lx %lx\n",
				   *(unsigned long *)&serial.printf,
				   serial_debug_serial_printf);
	serial.printf("TEST IS WORKING\n");
	serial_debug_serial_printf("TEST IS DONE\n");
	serial_debug_serial_printf("Metadata Name %lx\n",
				   BootInfo->memoryMapSize);
	serial_debug_serial_printf("Setup registry\n");
	while (1)
		;
}
