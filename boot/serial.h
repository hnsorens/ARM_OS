#ifndef SERIAL_H
#define SERIAL_H

#include <efi.h>

UINTN
Boot_Log(
        IN CONST CHAR8 *Str, 
        IN UINTN N
);

VOID
Boot_Log_Start();


int serial_debug_serial_printf(const char *format, ...);


#endif
