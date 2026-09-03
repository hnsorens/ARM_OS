/*
 * Exercises anonymous mmap / munmap / mprotect / brk from EL0. Loaded and
 * run by the elf_loader `/mmtest` kernelTest. No libc.
 *
 * Exit codes: 40 = all good; 21..31 = a specific check failed.
 */

#define SYS_munmap 215
#define SYS_brk 214
#define SYS_mmap 222
#define SYS_mprotect 226
#define SYS_write 64
#define SYS_exit 93

#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20

static long sc6(long nr, long a0, long a1, long a2, long a3, long a4, long a5)
{
    register long x8 __asm__("x8") = nr;
    register long x0 __asm__("x0") = a0;
    register long x1 __asm__("x1") = a1;
    register long x2 __asm__("x2") = a2;
    register long x3 __asm__("x3") = a3;
    register long x4 __asm__("x4") = a4;
    register long x5 __asm__("x5") = a5;
    __asm__ volatile("svc #0"
                     : "+r"(x0)
                     : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
                     : "memory");
    return x0;
}

static void put(const char *s)
{
    long n = 0;
    while (s[n])
        n++;
    sc6(SYS_write, 1, (long)s, n, 0, 0, 0);
}

static void exit_(long c)
{
    sc6(SYS_exit, c, 0, 0, 0, 0, 0);
    for (;;) {
    }
}

void _start(void)
{
    const long len = 64 * 1024;

    long p = sc6(SYS_mmap, 0, len, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p <= 0)
        exit_(21);
    volatile unsigned char *m = (volatile unsigned char *)p;

    for (long i = 0; i < len; i++)
        if (m[i] != 0)
            exit_(22);
    for (long i = 0; i < len; i += 4096)
        m[i] = (unsigned char)(i / 4096 + 1);
    for (long i = 0; i < len; i += 4096)
        if (m[i] != (unsigned char)(i / 4096 + 1))
            exit_(23);

    long q = sc6(SYS_mmap, 0, 4096, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (q <= 0 || q == p)
        exit_(24);
    *(volatile unsigned char *)q = 0xAB;
    if (*(volatile unsigned char *)q != 0xAB)
        exit_(25);

    if (sc6(SYS_munmap, p, len, 0, 0, 0, 0) != 0)
        exit_(26);

    long b0 = sc6(SYS_brk, 0, 0, 0, 0, 0, 0);
    if (b0 <= 0)
        exit_(27);
    long b1 = sc6(SYS_brk, b0 + 8192, 0, 0, 0, 0, 0);
    if (b1 != b0 + 8192)
        exit_(28);
    volatile unsigned char *bp = (volatile unsigned char *)b0;
    bp[0] = 'x';
    bp[8191] = 'y';
    if (bp[0] != 'x' || bp[8191] != 'y')
        exit_(29);
    long b2 = sc6(SYS_brk, b0, 0, 0, 0, 0, 0);
    if (b2 != b0)
        exit_(30);

    if (sc6(SYS_mprotect, q, 4096, PROT_READ, 0, 0, 0) != 0)
        exit_(31);
    if (sc6(SYS_mprotect, q, 4096, PROT_READ | PROT_WRITE, 0, 0, 0) != 0)
        exit_(31);
    if (sc6(SYS_munmap, q, 4096, 0, 0, 0, 0) != 0)
        exit_(31);

    put("[mmtest] ok\n");
    exit_(40);
}
