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

UINTN
Ok_Log(IN CONST CHAR8 *Str, IN UINTN N);

UINTN
Fail_Log(IN CONST CHAR8 *Str, IN UINTN N);
VOID Boot_Log_Hex(IN UINT64 Val);
#endif
