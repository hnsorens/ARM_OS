/*
 * Verifies nanosleep actually waits: sleep 50 ms and check the monotonic
 * clock advanced by at least ~40 ms (slack for a coarse counter) and by
 * less than 5 s (didn't hang). Then the same for clock_nanosleep. All
 * buffers are stack locals on purpose -- this also exercises that a
 * long-running syscall handler doesn't corrupt the caller's user stack.
 * No libc.
 *
 * Exit 77 = pass; 71.. = which check failed.
 */

#define SYS_write 64
#define SYS_nanosleep 101
#define SYS_clock_gettime 113
#define SYS_clock_nanosleep 115
#define SYS_exit 93

#define CLOCK_MONOTONIC 1

static long sc6(long nr, long a0, long a1, long a2, long a3, long a4, long a5)
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

static long slen(const char *s) { long n = 0; while (s[n]) n++; return n; }
static void put(const char *s) { sc6(SYS_write, 1, (long)s, slen(s), 0, 0, 0); }
static void exit_(long c) { sc6(SYS_exit, c, 0, 0, 0, 0, 0); for (;;) {} }

struct timespec { long tv_sec; long tv_nsec; };

static long now_ns(void)
{
    struct timespec t = {0, 0};
    sc6(SYS_clock_gettime, CLOCK_MONOTONIC, (long)&t, 0, 0, 0, 0);
    return t.tv_sec * 1000000000L + t.tv_nsec;
}

void _start(void)
{
    put("[sleeptest] start\n");

    long t0 = now_ns();
    struct timespec req = {0, 50000000L}; /* 50 ms */
    if (sc6(SYS_nanosleep, (long)&req, 0, 0, 0, 0, 0) != 0) exit_(71);
    long dt = now_ns() - t0;
    if (dt < 40000000L) exit_(72);   /* woke far too early */
    if (dt > 5000000000L) exit_(73); /* hung */

    long t2 = now_ns();
    struct timespec req2 = {0, 30000000L}; /* 30 ms */
    if (sc6(SYS_clock_nanosleep, CLOCK_MONOTONIC, 0, (long)&req2, 0, 0, 0) != 0) exit_(74);
    if (now_ns() - t2 < 20000000L) exit_(75);

    put("[sleeptest] ok\n");
    exit_(77);
}
