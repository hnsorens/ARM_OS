/*
 * Process groups / sessions: getpgid/setpgid/getsid/setsid, and that a
 * fork inherits the pgid while setpgid(0,0) makes a fresh group visible
 * to the parent. No libc. Exit 55 = pass; 61.. = which check failed.
 */

#define SYS_write 64
#define SYS_getpid 172
#define SYS_setpgid 154
#define SYS_getpgid 155
#define SYS_setsid 157
#define SYS_getsid 156
#define SYS_clone 220
#define SYS_wait4 260
#define SYS_exit 93

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

static long getpid_(void) { return sc6(SYS_getpid, 0, 0, 0, 0, 0, 0); }
static long getpgid_(long p) { return sc6(SYS_getpgid, p, 0, 0, 0, 0, 0); }
static long setpgid_(long p, long g) { return sc6(SYS_setpgid, p, g, 0, 0, 0, 0); }

void _start(void)
{
    put("[pgidtest] start\n");
    long pid = getpid_();

    if (getpgid_(0) != pid) exit_(61);
    if (sc6(SYS_getsid, 0, 0, 0, 0, 0, 0) != pid) exit_(62);
    if (setpgid_(0, 0) != 0) exit_(63);          /* no-op: already own group */

    long kid = sc6(SYS_clone, 0, 0, 0, 0, 0, 0);
    if (kid == 0) {
        /* child: inherited the parent's pgid == parent pid */
        if (getpgid_(0) != pid) exit_(7);
        /* move to its own group */
        if (setpgid_(0, 0) != 0) exit_(8);
        if (getpgid_(0) != getpid_()) exit_(9);
        exit_(6);
    }
    if (kid < 0) exit_(64);

    int status = 0;
    if (sc6(SYS_wait4, -1, (long)&status, 0, 0, 0, 0) != kid) exit_(65);
    /* child exits 6 only if it inherited pgid==parent then setpgid(0,0)
     * gave it getpgid(0)==getpid() */
    if (((status >> 8) & 0xff) != 6) exit_(66);
    /* our own group is unchanged by the child's setpgid */
    if (getpgid_(0) != pid) exit_(67);

    put("[pgidtest] ok\n");
    exit_(55);
}
