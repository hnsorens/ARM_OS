#ifndef SERIAL_DEBUG_API_H
#define SERIAL_DEBUG_API_H

typedef struct serial_module_interface
{
    int (*printf)(const char* format, ...);
} serial_module_interface_t;

#endif
