#ifndef __SERIAL_DEBUG_H__
#define __SERIAL_DEBUG_H__

#define DEBUG(fmt, ...) serial_debug_serial_printf("[" __MODULE_NAME_STR__ "] " fmt "\n", ##__VA_ARGS__)
#define ERROR(fmt, ...) serial_debug_serial_printf("ERROR: [" __MODULE_NAME_STR__ "] " fmt "\n", ##__VA_ARGS__)

#endif