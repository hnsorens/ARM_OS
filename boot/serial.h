#ifndef SERIAL_H
#define SERIAL_H

#include <efi.h>
#include <efilib.h>

UINTN
Boot_Log(
        IN CONST CHAR8 *Str, 
        IN UINTN N
);

VOID
Boot_Log_Start();


#endif
