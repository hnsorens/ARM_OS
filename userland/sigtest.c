/*
 * Exercises Layer 3 signal delivery from EL0: rt_sigaction, catching a
 * kill(), rt_sigprocmask block/pending/unblock, and a SIGSEGV handler.
 * Loaded by the elf_loader `/sigtest` kernelTest. No libc.
 *
 * Exit 88 = success (raised from the SIGSEGV handler); 71.. = a check
 * failed before the fault.
 */

#define SYS_rt_sigaction 134
#define SYS_rt_sigprocmask 135
#define SYS_rt_sigpending 136
#define SYS_rt_sigreturn 139
#define SYS_kill 129
#define SYS_getpid 172
#define SYS_write 64
#define SYS_exit 93

#define SIGUSR1 10
#define SIGSEGV 11
#define SA_SIGINFO 0x00000004
#define SA_RESTORER 0x04000000
#define SIG_BLOCK 0
#define SIG_UNBLOCK 1

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

/* kernel struct k_sigaction: { handler; flags; restorer; mask } */
struct ksa { unsigned long handler, flags, restorer, mask; };

__attribute__((naked)) static void restore_rt(void)
{
    __asm__ volatile("mov x8, #139\n\t"
                     "svc #0\n");
}

static volatile int g_hits = 0;
static volatile int g_last = 0;

__attribute__((used)) static void usr1_handler(int sig, void *info, void *uc)
{
    (void)info; (void)uc;
    g_hits++;
    g_last = sig;
}

__attribute__((used)) static void segv_handler(int sig, void *info, void *uc)
{
    (void)sig; (void)info; (void)uc;
    put("[sigtest] SIGSEGV handler reached\n");
    exit_(88);
}

static int install(int sig, void (*h)(int, void *, void *))
{
    struct ksa sa;
    sa.handler = (unsigned long)h;
    sa.flags = SA_SIGINFO | SA_RESTORER;
    sa.restorer = (unsigned long)restore_rt;
    sa.mask = 0;
    return (int)sc6(SYS_rt_sigaction, sig, (long)&sa, 0, 8, 0, 0);
}

void _start(void)
{
    long pid = sc6(SYS_getpid, 0, 0, 0, 0, 0, 0);

    /* 1. catch SIGUSR1 */
    if (install(SIGUSR1, usr1_handler) != 0) exit_(71);
    if (sc6(SYS_kill, pid, SIGUSR1, 0, 0, 0, 0) != 0) exit_(72);
    if (g_hits != 1 || g_last != SIGUSR1) exit_(73);

    /* 2. block, raise (stays pending), check pending, unblock */
    unsigned long set = 1UL << (SIGUSR1 - 1);
    if (sc6(SYS_rt_sigprocmask, SIG_BLOCK, (long)&set, 0, 8, 0, 0) != 0) exit_(74);
    sc6(SYS_kill, pid, SIGUSR1, 0, 0, 0, 0);
    if (g_hits != 1) exit_(75); /* blocked -> not delivered yet */
    unsigned long pend = 0;
    sc6(SYS_rt_sigpending, (long)&pend, 0, 0, 8, 0, 0);
    if (!(pend & set)) exit_(76);
    sc6(SYS_rt_sigprocmask, SIG_UNBLOCK, (long)&set, 0, 8, 0, 0);
    /* delivered on the way out of the unblock syscall */
    if (g_hits != 2) exit_(77);

    /* 3. SIGSEGV from a bad store -> segv_handler -> exit(88) */
    if (install(SIGSEGV, segv_handler) != 0) exit_(78);
    *(volatile int *)0x1 = 0x1234;

    put("[sigtest] fault did not trap - FAIL\n");
    exit_(79);
}
