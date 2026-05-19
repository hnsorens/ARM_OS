#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <stddef.h>

void *memset(void *dest, int ch, size_t n)
{
    unsigned char *ptr = (unsigned char *)dest;
    while (n--)
    {
        *ptr++ = (unsigned char)ch;
    }
    return dest;
}

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }

    return dest;
}

#endif
