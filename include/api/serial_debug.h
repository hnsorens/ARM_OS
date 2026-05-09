#ifndef SERIAL_DEBUG_API_H
#define SERIAL_DEBUG_API_H

typedef struct serial_interface
{
    int (*printf)(const char* format, ...);
} serial_interface_t;

#endif
