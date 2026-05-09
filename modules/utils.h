#ifndef UTILS_H
#define UTILS_H

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
