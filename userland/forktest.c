/*
 * Exercises fork + wait + the process tree from EL0. Loaded and run by
 * the elf_loader `/forktest` kernelTest (like hello.c). No libc.
 *
 * The parent forks a child; the child prints its pid/ppid and exits with
 * a known code; the parent wait4()s, checks the reaped pid and the
 * Linux-style status, and exits 0 on success (non-zero otherwise) so the
 * kernel test can assert on it.
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
#define SYS_getpid 172
#define SYS_getppid 173
#define SYS_clone 220
#define SYS_exit 93
#define SYS_wait4 260

static long slen(const char *s)
{
    long n = 0;
    while (s[n])
        n++;
    return n;
}
static void put(const char *s) { sc6(SYS_write, 1, (long)s, slen(s), 0, 0, 0); }

static long fork_(void) { return sc6(SYS_clone, 0, 0, 0, 0, 0, 0); }
static long getpid_(void) { return sc6(SYS_getpid, 0, 0, 0, 0, 0, 0); }
static long getppid_(void) { return sc6(SYS_getppid, 0, 0, 0, 0, 0, 0); }
static void exit_(long c) { sc6(SYS_exit, c, 0, 0, 0, 0, 0); }
static long wait4_(long pid, int *status) { return sc6(SYS_wait4, pid, (long)status, 0, 0, 0, 0); }

#define CHILD_CODE 7

void _start(void)
{
    put("[forktest] start\n");

    long parent_pid = getpid_();
    long pid = fork_();

    if (pid == 0) {
        /* child */
        long myppid = getppid_();
        if (myppid != parent_pid) {
            put("[forktest] child: wrong ppid\n");
            exit_(2);
        }
        put("[forktest] child running, exiting 7\n");
        exit_(CHILD_CODE);
    }

    /* parent */
    if (pid <= 0) {
        put("[forktest] fork failed\n");
        exit_(3);
    }

    int status = 0;
    long w = wait4_(-1, &status);
    if (w != pid) {
        put("[forktest] wait: wrong pid\n");
        exit_(4);
    }
    if (((status >> 8) & 0xff) != CHILD_CODE) {
        put("[forktest] wait: wrong status\n");
        exit_(5);
    }

    put("[forktest] parent: child reaped ok, exiting 0\n");
    exit_(0);

    for (;;) {
    }
}
