#include "utilities.h"

void *memcpy(void *dest, const void *src, unsigned long n)
{
	unsigned char *d = (unsigned char *)dest;
	const unsigned char *s = (const unsigned char *)src;

	for (unsigned long i = 0; i < n; ++i) {
		d[i] = s[i];
	}

	return dest;
}

void *memset(void *s, int c, unsigned long n)
{
	unsigned char *p = (unsigned char *)s;

	unsigned char fill = (unsigned char)c;

	for (unsigned long i = 0; i < n; ++i) {
		p[i] = fill;
	}

	return s;
}
