

#include <modules.h>
#include <api/gic_v3.h>
#include "gic_v3.h"
#include <api/serial_debug.h>

IMPORT_INTERFACE_ANY(serial, serial);

int main()
{
	driver_timer_start_sequence();
	return 0;
}

EXPORT_INTERFACE(
	irq_chip, gic_v3,
	{ .enable = enable, .disable = disable, .ack = ack, .eoi = eoi });
