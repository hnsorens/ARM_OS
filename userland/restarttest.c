/*
 * SA_RESTART: a child blocks in ppoll(infinite) on an empty pipe; the
 * parent sends SIGUSR1 (handler installed with SA_RESTART). The handler
 * runs and ppoll is *restarted* rather than returning EINTR, so once the
 * parent writes the pipe ppoll returns 1 and the child exits 42.
 * No libc. Exit 55 = pass; 61.. = which check failed.
 */

#define SYS_write 64
#define SYS_close 57
#define SYS_ppoll 73
#define SYS_pipe2 59
#define SYS_sched_yield 124
#define SYS_rt_sigaction 134
#define SYS_kill 129
#define SYS_clone 220
#define SYS_wait4 260
#define SYS_exit 93

#define SIGUSR1 10
#define POLLIN 0x001
#define SA_SIGINFO 0x00000004
#define SA_RESTART 0x10000000
#define SA_RESTORER 0x04000000

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
static void yield_(void) { sc6(SYS_sched_yield, 0, 0, 0, 0, 0, 0); }

struct pollfd { int fd; short events; short revents; };
struct ksa { unsigned long handler, flags, restorer, mask; };

__attribute__((naked)) static void restore_rt(void)
{
    __asm__ volatile("mov x8, #139\n\tsvc #0\n");
}

static volatile int g_ran = 0;
__attribute__((used)) static void usr1(int s, void *a, void *b)
{
    (void)s; (void)a; (void)b;
    g_ran = 1;
}

void _start(void)
{
    put("[restarttest] start\n");

    int p[2];
    if (sc6(SYS_pipe2, (long)p, 0, 0, 0, 0, 0) != 0) exit_(61);

    long kid = sc6(SYS_clone, 0, 0, 0, 0, 0, 0);
    if (kid == 0) {
        struct ksa sa = {(unsigned long)usr1, SA_SIGINFO | SA_RESTART | SA_RESTORER,
                         (unsigned long)restore_rt, 0};
        if (sc6(SYS_rt_sigaction, SIGUSR1, (long)&sa, 0, 8, 0, 0) != 0) exit_(1);
        struct pollfd pf = {p[0], POLLIN, 0};
        long r = sc6(SYS_ppoll, (long)&pf, 1, 0, 0, 0, 0); /* infinite */
        if (r == 1 && g_ran) exit_(42);
        if (r < 0) exit_(43);   /* EINTR -> SA_RESTART didn't work */
        exit_(44);
    }
    if (kid < 0) exit_(62);

    /* let the child install its handler and block in ppoll */
    for (int i = 0; i < 8; i++) yield_();
    /* interrupt it: handler runs, ppoll must restart (not return) */
    if (sc6(SYS_kill, kid, SIGUSR1, 0, 0, 0, 0) != 0) exit_(63);
    for (int i = 0; i < 8; i++) yield_();
    /* now satisfy the poll */
    if (sc6(SYS_write, p[1], (long)"x", 1, 0, 0, 0) != 1) exit_(64);

    int st = 0;
    if (sc6(SYS_wait4, kid, (long)&st, 0, 0, 0, 0) != kid) exit_(65);
    if (((st >> 8) & 0xff) != 42) exit_(66);

    sc6(SYS_close, p[0], 0, 0, 0, 0, 0);
    sc6(SYS_close, p[1], 0, 0, 0, 0, 0);
    put("[restarttest] ok\n");
    exit_(55);
}
