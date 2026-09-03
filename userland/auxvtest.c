/*
 * Validates the SysV/AArch64 initial process stack the elf_loader builds:
 * argc/argv, then (after envp) the auxv. Loaded and run by the elf_loader
 * `/auxvtest` kernelTest. No libc -- syscalls go straight through svc #0.
 *
 * Exit codes: 44 = all good; 31..38 = a specific check failed.
 */

#define SYS_write 64
#define SYS_exit 93

static long sc3(long nr, long a0, long a1, long a2)
{
    register long x8 __asm__("x8") = nr;
    register long x0 __asm__("x0") = a0;
    register long x1 __asm__("x1") = a1;
    register long x2 __asm__("x2") = a2;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return x0;
}

static void put(const char *s)
{
    long n = 0;
    while (s[n])
        n++;
    sc3(SYS_write, 1, (long)s, n);
}

static void exit_(long c)
{
    sc3(SYS_exit, c, 0, 0);
    for (;;) {
    }
}

/* AArch64 auxv keys */
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define AT_PAGESZ 6
#define AT_ENTRY 9
#define AT_RANDOM 25

__attribute__((used)) static void start_main(unsigned long *sp)
{
    long argc = (long)sp[0];
    char **argv = (char **)&sp[1];
    char **envp = argv + argc + 1;

    unsigned long *aux = (unsigned long *)envp;
    while (*aux) /* skip the (empty) environment */
        aux++;
    aux++; /* now at the first auxv pair */

    long pagesz = -1, entry = 0, phnum = 0, phent = 0;
    unsigned long phdr = 0, rnd = 0;
    for (; aux[0] != 0; aux += 2) {
        switch (aux[0]) {
        case AT_PHDR:   phdr = aux[1]; break;
        case AT_PHENT:  phent = (long)aux[1]; break;
        case AT_PHNUM:  phnum = (long)aux[1]; break;
        case AT_PAGESZ: pagesz = (long)aux[1]; break;
        case AT_ENTRY:  entry = (long)aux[1]; break;
        case AT_RANDOM: rnd = aux[1]; break;
        }
    }

    if (argc < 1)
        exit_(31);

    /* argv[0] must be exactly "/auxvtest" */
    const char *want = "/auxvtest";
    const char *got = argv[0];
    for (long i = 0; want[i] || got[i]; i++)
        if (want[i] != got[i])
            exit_(32);

    if (pagesz != 4096)
        exit_(33);
    if (entry == 0)
        exit_(34);
    if (phent != 56)
        exit_(35);
    if (phnum == 0)
        exit_(36);
    if (phdr == 0)
        exit_(37);
    if (rnd == 0)
        exit_(38);

    /* the AT_RANDOM pointer must be readable */
    volatile unsigned char *rb = (volatile unsigned char *)rnd;
    unsigned acc = 0;
    for (int i = 0; i < 16; i++)
        acc += rb[i];
    (void)acc;

    put("[auxvtest] ok\n");
    exit_(44);
}

__attribute__((naked)) void _start(void)
{
    __asm__ volatile("mov x0, sp\n\t"
                     "b start_main\n");
}
