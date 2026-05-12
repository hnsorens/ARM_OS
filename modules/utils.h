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

#endif
