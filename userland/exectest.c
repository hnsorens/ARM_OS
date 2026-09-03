/*
 * Exercises execve from EL0. Loaded and run by the elf_loader
 * `/exectest` kernelTest (like forktest.c). No libc.
 *
 *   1. execve a nonexistent path -> must return -errno, caller continues.
 *   2. execve /hello with an argv -> replaces this image in place (same
 *      pid). /hello prints "hello from EL0" and exits 100 + getpid();
 *      since the pid is unchanged, the kernel test sees exit_code
 *      100 + <the pid it loaded /exectest as>.
 */

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

#define SYS_write 64
#define SYS_execve 221
#define SYS_exit 93

static long slen(const char *s)
{
    long n = 0;
    while (s[n])
        n++;
    return n;
}
static void put(const char *s) { sc6(SYS_write, 1, (long)s, slen(s), 0, 0, 0); }
static long execve_(const char *p, char **argv, char **envp)
{
    return sc6(SYS_execve, (long)p, (long)argv, (long)envp, 0, 0, 0);
}
static void exit_(long c) { sc6(SYS_exit, c, 0, 0, 0, 0, 0); }

void _start(void)
{
    put("[exectest] start\n");

    long r = execve_("/no_such_file", 0, 0);
    if (r >= 0) {
        put("[exectest] bad execve should have failed\n");
        exit_(2);
    }
    put("[exectest] bad execve failed as expected; now real execve\n");

    char *argv[] = {"hello", "and-an-arg", 0};
    execve_("/hello", argv, 0);

    /* only reached if execve failed */
    put("[exectest] real execve returned - FAIL\n");
    exit_(3);
}
