/*
 * The init process: the first thing the kernel runs at EL0 once module
 * bring-up (and the test suite) has finished. See bootloader/main.zig's
 * startInit(). No libc -- one execve into an interactive shell.
 *
 * Prefers GNU bash, falls back to BusyBox ash, and if neither is present
 * drops to a tiny built-in read/echo loop so a booted image is never
 * dead. The test suite has already printed its [SUMMARY] by the time
 * this runs, so blocking on the console here is fine.
 */

static long sc(long nr, long a0, long a1, long a2, long a3, long a4, long a5)
{
    register long x8 __asm__("x8") = nr;
    register long x0 __asm__("x0") = a0;
    register long x1 __asm__("x1") = a1;
    register long x2 __asm__("x2") = a2;
    register long x3 __asm__("x3") = a3;
    register long x4 __asm__("x4") = a4;
    register long x5 __asm__("x5") = a5;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory");
    return x0;
}

#define SYS_read 63
#define SYS_write 64
#define SYS_execve 221
#define SYS_exit 93

static long slen(const char *s) { long n = 0; while (s[n]) n++; return n; }
static void put(const char *s) { sc(SYS_write, 1, (long)s, slen(s), 0, 0, 0); }

static char *env[] = {
    "PATH=/bin", "HOME=/", "TERM=linux", "PS1=arm-os:\\w\\$ ", 0,
};

static void try_exec(const char *path, char *const *argv)
{
    sc(SYS_execve, (long)path, (long)argv, (long)env, 0, 0, 0);
    /* only returns on failure */
}

void _start(void)
{
    put("\n=== ARM_OS ===  starting shell\n");

    char *bash_av[] = {"-bash", 0};
    try_exec("/bin/bash", bash_av);

    char *sh_av[] = {"-sh", 0};
    try_exec("/bin/busybox", sh_av);
    try_exec("/bin/sh", sh_av);

    /* no shell found -- minimal read/echo loop */
    put("no /bin/bash or /bin/sh; falling back to echo loop (^D exits)\n> ");
    char line[256];
    int len = 0;
    for (;;) {
        char c;
        long r = sc(SYS_read, 0, (long)&c, 1, 0, 0, 0);
        if (r <= 0 || c == 4) { put("\nbye\n"); sc(SYS_exit, 0, 0, 0, 0, 0, 0); }
        if (c == '\n') {
            line[len] = 0;
            put("you said: ");
            sc(SYS_write, 1, (long)line, len, 0, 0, 0);
            put("\n> ");
            len = 0;
        } else if (c == 0x7f || c == 8) {
            if (len > 0) len--;
        } else if (len < (int)sizeof(line) - 1) {
            line[len++] = c;
        }
    }
}
