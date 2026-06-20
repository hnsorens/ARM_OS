/**
 * @file kernel_types_h
 * @brief Global Architectural Primitive Type Definitions.
 *
 * Provides concise, explicit standard width aliases for integer primitives 
 * to ensure uniform storage allocation sizes across kernel-space subsystems.
 */

#ifndef KERNEL_TYPES_H
#define KERNEL_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* --- Unsigned Fixed-Width Integer Types --- */
typedef uint8_t  u8;   /**<  8-bit unsigned byte      */
typedef uint16_t u16;  /**< 16-bit unsigned half-word */
typedef uint32_t u32;  /**< 32-bit unsigned word      */
typedef uint64_t u64;  /**< 64-bit unsigned double-word*/

/* --- Signed Fixed-Width Integer Types --- */
typedef int8_t   s8;   /**<  8-bit signed byte        */
typedef int16_t  s16;  /**< 16-bit signed half-word   */
typedef int32_t  s32;  /**< 32-bit signed word        */
typedef int64_t  s64;  /**< 64-bit signed double-word */

#endif /* KERNEL_TYPES_H */
