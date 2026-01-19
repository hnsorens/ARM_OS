


#include "module.h"

#include "modules/kmm.h"

vtable(str_vtable_t);
start(init, str_fetch, str_init);

/* Memory Functions */
void* memset(void* s, int c, unsigned long n) {
    unsigned char* p = (unsigned char*)s;
    for (unsigned long i = 0; i < n; i++) {
        p[i] = (unsigned char)c;
    }
    return s;
}

void* memcpy(void* dest, const void* src, unsigned long n) {
    char* d = (char*)dest;
    const char* s = (const char*)src;
    
    for (unsigned long i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

void* memmove(void* dest, const void* src, unsigned long n) {
    char* d = (char*)dest;
    const char* s = (const char*)src;
    
    if (d < s) {
        /* Copy forward */
        for (unsigned long i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        /* Copy backward */
        for (unsigned long i = n; i > 0; i--) {
            d[i-1] = s[i-1];
        }
    }
    return dest;
}

int memcmp(const void* s1, const void* s2, unsigned long n) {
    const unsigned char* p1 = (const unsigned char*)s1;
    const unsigned char* p2 = (const unsigned char*)s2;
    
    for (unsigned long i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] - p2[i];
        }
    }
    return 0;
}

void* memchr(const void* s, int c, unsigned long n) {
    const unsigned char* p = (const unsigned char*)s;
    unsigned char ch = (unsigned char)c;
    
    for (unsigned long i = 0; i < n; i++) {
        if (p[i] == ch) {
            return (void*)(p + i);
        }
    }
    return 0;
}

/* String Functions */
unsigned long strlen(const char* s) {
    unsigned long len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while (*src) {
        *d++ = *src++;
    }
    *d = '\0';
    return dest;
}

char* strncpy(char* dest, const char* src, unsigned long n) {
    unsigned long i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

char* strcat(char* dest, const char* src) {
    char* d = dest;
    
    /* Find end of dest */
    while (*d) {
        d++;
    }
    
    /* Copy src */
    while (*src) {
        *d++ = *src++;
    }
    *d = '\0';
    return dest;
}

char* strncat(char* dest, const char* src, unsigned long n) {
    char* d = dest;
    
    /* Find end of dest */
    while (*d) {
        d++;
    }
    
    /* Copy up to n chars from src */
    unsigned long i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        d[i] = src[i];
    }
    d[i] = '\0';
    return dest;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, unsigned long n) {
    for (unsigned long i = 0; i < n; i++) {
        if (s1[i] != s2[i]) {
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        }
        if (s1[i] == '\0') {
            break;
        }
    }
    return 0;
}

char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == (char)c) {
            return (char*)s;
        }
        s++;
    }
    if (c == '\0') {
        return (char*)s;
    }
    return 0;
}

char* strrchr(const char* s, int c) {
    const char* last = 0;
    
    while (*s) {
        if (*s == (char)c) {
            last = s;
        }
        s++;
    }
    
    if (c == '\0') {
        return (char*)s;
    }
    return (char*)last;
}

char* strstr(const char* haystack, const char* needle) {
    if (*needle == '\0') {
        return (char*)haystack;
    }
    
    for (; *haystack; haystack++) {
        const char* h = haystack;
        const char* n = needle;
        
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        
        if (*n == '\0') {
            return (char*)haystack;
        }
    }
    return 0;
}

/* Utility Functions */
char* strdup(const char* s) {
    unsigned long len = strlen(s) + 1;
    char* new_str = (char*)kmm_kmalloc(len);
    if (new_str) {
        strcpy(new_str, s);
    }
    return new_str;
}

void str_init(kernel_vtable_t *kvtable)
{
}

void str_fetch(kernel_vtable_t *kvtable)
{
    kmm_fetch(kvtable);
    
}

void init(str_vtable_t *vtable)
{
//   vtable->memset = memset;
//   vtable->memcpy = memcpy;
//   vtable->memmove = memmove;
//   vtable->memcmp = memcmp;
//   vtable->memchr = memchr;

//   vtable->strlen = strlen;
//   vtable->strncpy = strncpy;
//   vtable->strcat = strcat;
//   vtable->strncat = strncat;
//   vtable->strcmp = strcmp;
//   vtable->strncmp = strncmp;
//   vtable->strchr = strchr;
//   vtable->strrchr = strrchr;
//   vtable->strstr = strstr;
  
//   vtable->strdup = strdup;
}
