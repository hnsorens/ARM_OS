#ifndef __SERIAL_DEBUG_IMPL_T__
#define __SERIAL_DEBUG_IMPL_T__

#include "serial_debug_driver.h"
#include "serial_debug.h"

#define __MODULE_NAME__ SERIAL_DEBUG
#define __MODULE_NAME_STR__ "SERIAL_DEBUG"
#define __MAIN__

serial_debug_ops __serial_debug__;
serial_debug_driver __serial_debug_ops__;

unsigned long __load_offset__;

void serial_debug_init(serial_debug_ops* serial_debug);
void serial_debug_fetch(core_ops* ops);
void serial_debug_start(core_ops* ops);

__attribute__((section(".text._entry")))
serial_debug_driver *_entry(unsigned long offset) {
	__load_offset__ = offset;

	serial_debug_init(&__serial_debug__);
	__serial_debug_ops__.serial_debug = &__serial_debug__;


	__serial_debug_ops__.start = serial_debug_start;
	__serial_debug_ops__.fetch = serial_debug_fetch;

	return &__serial_debug_ops__;
}

#endif