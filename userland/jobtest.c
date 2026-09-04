/*
 * Job control: parent SIGSTOPs a forked child, wait4(WUNTRACED) reports
 * it stopped, parent SIGCONTs it, the child's SIGCONT handler runs and it
 * exits 33, parent reaps it. No libc. Exit 55 = pass; 61.. = which check.
 */

#define SYS_write 64
#define SYS_sched_yield 124
#define SYS_rt_sigaction 134
#define SYS_kill 129
#define SYS_getpid 172
#define SYS_clone 220
#define SYS_wait4 260
#define SYS_exit 93

#define SIGCONT 18
#define SIGSTOP 19
#define SA_SIGINFO 0x00000004
#define SA_RESTORER 0x04000000
#define WUNTRACED 2

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

struct ksa { unsigned long handler, flags, restorer, mask; };

__attribute__((naked)) static void restore_rt(void)
{
    __asm__ volatile("mov x8, #139\n\tsvc #0\n");
}

static volatile int g_cont = 0;
__attribute__((used)) static void cont_handler(int s, void *a, void *b)
{
    (void)s; (void)a; (void)b;
    g_cont = 1;
}

void _start(void)
{
    put("[jobtest] start\n");

    long kid = sc6(SYS_clone, 0, 0, 0, 0, 0, 0);
    if (kid == 0) {
        struct ksa sa = {(unsigned long)cont_handler, SA_SIGINFO | SA_RESTORER,
                         (unsigned long)restore_rt, 0};
        sc6(SYS_rt_sigaction, SIGCONT, (long)&sa, 0, 8, 0, 0);
        for (;;) {
            yield_();
            if (g_cont) exit_(33);
        }
    }
    if (kid < 0) exit_(61);

    /* parent: stop the child, confirm wait4(WUNTRACED) reports it */
    if (sc6(SYS_kill, kid, SIGSTOP, 0, 0, 0, 0) != 0) exit_(62);
    int st = 0;
    long w = sc6(SYS_wait4, kid, (long)&st, WUNTRACED, 0, 0, 0);
    if (w != kid) exit_(63);
    if ((st & 0xff) != 0x7f) exit_(64);       /* W_STOPPED */
    if (((st >> 8) & 0xff) != SIGSTOP) exit_(65);

    /* resume it; it should run its SIGCONT handler and exit 33 */
    if (sc6(SYS_kill, kid, SIGCONT, 0, 0, 0, 0) != 0) exit_(66);
    st = 0;
    w = sc6(SYS_wait4, kid, (long)&st, 0, 0, 0, 0);
    if (w != kid) exit_(67);
    if (((st >> 8) & 0xff) != 33) exit_(68);

    put("[jobtest] ok\n");
    exit_(55);
}
