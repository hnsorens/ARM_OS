

#include <math.h>
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
	interrupt_manager, gic_v3, {
    .init_global = init_global,
    .init_core = init_core,
    .set_core_priority_mask = set_core_priority_mask,
    .enable = enable,
    .disable = disable,
    .configure = configure,
    .set_group = set_group,
    .route_to_core = route_to_core,
    .acknowledge = ack,
    .end_of_interrupt = eoi,
    .register_handler = register_handler,
    .unregister_handler = unregister_handler
    });

