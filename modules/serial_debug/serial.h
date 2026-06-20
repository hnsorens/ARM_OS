/**
 * @file serial.h
 * @brief Public interface definitions for the PrimeCell PL011 UART Serial Driver.
 */

#ifndef SERIAL_H
#define SERIAL_H

#include <api/serial_debug.h>

/**
 * @brief Formats and outputs a variable argument string directly to UART0.
 * * Implements standard formatting rules supporting custom pad sizing, justifications, 
 * radix transformations, and sub-byte type length modifier parsing.
 * * @param[in] format Formatted ASCII character string.
 * @param[in] ...    Variable arguments matching format specifier keys.
 * @return int       Total number of individual ASCII byte primitives written to MMIO registers.
 */
int serial_debug_serial_printf(const char *format, ...);

#endif /* SERIAL_H */
