#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <stddef.h>
#include "../include/type.h"

#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)

void* kmemset(void* dest, int ch, u64 count) {
    unsigned char* ptr = (unsigned char*)dest;
    
    // Fill a 64-bit variable with the byte pattern (e.g., all 0s)
    u64 val = (unsigned char)ch;
    val |= (val << 8);
    val |= (val << 16);
    val |= (val << 32);

    // 1. Align to 16 bytes for optimal bus speed
    while (((uintptr_t)ptr & 15) && count > 0) {
        *ptr++ = (unsigned char)ch;
        count--;
    }

    // 2. Blast 32 bytes at a time using ARM64 inline assembly
    while (count >= 32) {
        asm volatile(
            "stp %1, %1, [%0], #16 \n\t" // Store two 64-bit registers (16 bytes) and post-increment
            "stp %1, %1, [%0], #16 \n\t" // Store another two (16 bytes) and post-increment
            : "+r"(ptr)
            : "r"(val)
            : "memory"
        );
        count -= 32;
    }

    // 3. Clean up remaining bytes
    while (count > 0) {
        *ptr++ = (unsigned char)ch;
        count--;
    }

    return dest;
}

void *kmemcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }

    return dest;
}

#endif
