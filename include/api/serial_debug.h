/**
 * @file serial_debug_api.h
 * @brief Unified Virtual Function Table Interface definition for Kernel Serial Debugging.
 *
 * Provides an isolated procedural hook mapping standard formatting strings onto active
 * hardware communication buses (e.g., ARM PL011 UART). Allows high-level subsystems to 
 * perform synchronous raw I/O operations without maintaining hardwired direct register locks.
 */

#ifndef SERIAL_DEBUG_API_H
#define SERIAL_DEBUG_API_H

/**
 * @struct serial_interface
 * @brief Global system operational interface table for low-level serial output.
 *
 * Encapsulates standard variadic formatting mechanisms to serve as the baseline kernel text 
 * output engine. Implementations handle formatting flags, padding, and direct Memory-Mapped 
 * I/O (MMIO) transactional sequences under the hood.
 */
typedef struct serial_interface
{
    /**
     * @brief Formats and writes an arbitrary variadic argument sequence directly out to serial hardware.
     *
     * Processes classical printf specifiers (e.g., `%d`, `%x`, `%s`, `%p`) along with custom width, 
     * precision, padding, and length modifier properties (`h`, `l`, `ll`, `z`). Translates line
     * breaks (`\n`) into standard carriage return / line feed couples (`\r\n`) to preserve terminal alignment.
     *
     * @param[in] format Null-terminated ASCII character format string containing token directives.
     * @param[in] ...    Variable-length argument list matched up to corresponding format placeholders.
     *
     * @return int       The absolute number of individual raw byte primitives written to the MMIO data registers.
     */
    int (*printf)(const char* format, ...);
    
} serial_interface_t;

#endif /* SERIAL_DEBUG_API_H */
