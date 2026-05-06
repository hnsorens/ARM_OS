#ifndef SERIAL_H
#define SERIAL_H

#include "../../include/metadata.h"
#include "../../include/api/serial_debug.h"

int serial_debug_serial_printf(const char *format, ...);

#define REGISTER_MODULE(EXT, interface_type, ...) \
    /* Create the unique implementation struct */ \
    static const interface_type __impl__ = __VA_ARGS__; \
    \
    /* Create the unique metadata entry */ \
    __attribute__ ((section(".export.serial.first_serial_hehe"), used, aligned(8))) \
    static const Metadata __meta__ = { \
        .identifier = EXT & interface_type##_id, \
        .vtable = (void*)&__impl__ \
    }

#endif
