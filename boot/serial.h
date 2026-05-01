#ifndef SERIAL_H
#define SERIAL_H

#include <efi.h>
#include <efilib.h>

void serial_debug_start();

int serial_debug_serial_printf(char *format, ...);

#endif
