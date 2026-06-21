/**
 * @file main.c
 * @brief Driver Core registration matching interface linkage models.
 */

#include "serial.h"
#include <modules.h>

/**
 * @brief Exposes the initialization vector framework out to system registration link maps.
 * * Maps global interface functions directly to standard tracking tokens used by structural 
 * macros such as EXTERN_IMPORT_INTERFACE signatures.
 */
EXPORT_INTERFACE(serial, first_serial_hehe4,
		 { .printf = serial_debug_serial_printf })
