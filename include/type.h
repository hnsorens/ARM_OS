#ifndef KERNEL_TYPES_H
#define KERNEL_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef uint64_t phys_addr_t;
typedef uint64_t virt_addr_t;

#define k_error(status) (status)

/**
 * @brief Universal Kernel Status Codes
 * Every non-boolean function in the kernel should return this type.
 */
typedef enum k_status 
{
    /* --- SUCCESS --- */
    K_STATUS_OK = 0,                // Operation completed successfully

    /* --- GENERIC ERRORS (1-9) --- */
    K_STATUS_ERROR           = 1,   // Unspecified internal error
    K_STATUS_NOT_IMPLEMENTED = 2,   // Functionality is a stub/placeholder
    K_STATUS_NOT_SUPPORTED   = 3,   // Hardware or mode does not support this
    K_STATUS_INVALID_ARG     = 4,   // Null pointer or illegal value
    K_STATUS_BUSY            = 5,   // Resource is locked or in use
    K_STATUS_TIMEOUT         = 6,   // Hardware/Sync primitive timed out
    K_STATUS_CANCELED        = 7,   // Operation aborted by user/system
    K_STATUS_RETRY           = 8,   // Temporary failure, try again

    /* --- MEMORY ERRORS (10-29) --- */
    K_STATUS_OUT_OF_MEMORY   = 10,  // Physical RAM exhausted (PMM)
    K_STATUS_OUT_OF_VIRTUAL  = 11,  // Virtual address space exhausted (VMM)
    K_STATUS_ALREADY_MAPPED  = 12,  // Virtual address already has a physical counterpart
    K_STATUS_NOT_MAPPED      = 13,  // Virtual address has no physical counterpart
    K_STATUS_ALIGNMENT_ERROR = 14,  // Address is not aligned to page/boundary requirements
    K_STATUS_PERMISSION_DENIED = 15,// Violation of R/W/X or User/Kernel protection
    K_STATUS_TABLE_FULL      = 16,  // Page tables cannot accommodate more entries
    K_STATUS_BAD_PHYS_ADDR   = 17,  // Physical address is outside of known RAM
    K_STATUS_BAD_VIRT_ADDR   = 18,  // Virtual address is in a reserved/illegal range

    /* --- I/O & DEVICE ERRORS (30-49) --- */
    K_STATUS_IO_ERROR        = 30,  // General hardware communication failure
    K_STATUS_NOT_FOUND       = 31,  // Device, File, or Object does not exist
    K_STATUS_ALREADY_EXISTS  = 32,  // Unique resource (like a device name) is taken
    K_STATUS_DEVICE_OFFLINE  = 33,  // Device is not powered or disconnected
    K_STATUS_READ_ONLY       = 34,  // Attempted to write to read-only media/register
    K_STATUS_CHECKSUM_ERROR  = 35,  // Data corruption detected
    K_STATUS_OVERFLOW        = 36,  // Data exceeds buffer/register capacity

    /* --- SCHEDULING & IPC (50-69) --- */
    K_STATUS_BAD_STATE       = 50,  // Process/Thread is in the wrong state for this op
    K_STATUS_LIMIT_REACHED   = 51,  // Max processes/threads/handles reached
    K_STATUS_DEADLOCK        = 52,  // Circular dependency detected
    K_STATUS_INTERRUPTED     = 53,  // Operation broken by a signal/interrupt
    K_STATUS_NOT_READY       = 54,  // Task or resource is still initializing

    /* --- ARCHITECTURE SPECIFIC (70+) --- */
    K_STATUS_ILLEGAL_INST    = 70,  // CPU encountered an invalid instruction
    K_STATUS_STACK_OVERFLOW  = 71   // Kernel or User stack guard page hit

} k_status_t;

#endif
