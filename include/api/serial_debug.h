#ifndef SERIAL_DEBUG_API_H
#define SERIAL_DEBUG_API_H

#define SerialDeviceInterface_id 0

typedef struct SerialDeviceInterface
{
    int (*printf)(const char* format, ...);
} SerialDeviceInterface;

#endif
