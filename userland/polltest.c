/*
 * ppoll(2) on a pipe and the tty: readiness before/after a write, POLLHUP
 * on the read end once the write end closes, POLLOUT on stdout. No libc.
 * Exit 55 = pass; 61.. = which check failed.
 */

#define SYS_write 64
#define SYS_close 57
#define SYS_ppoll 73
#define SYS_pipe2 59
#define SYS_exit 93

#define POLLIN 0x001
#define POLLOUT 0x004
#define POLLHUP 0x010

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

struct pollfd { int fd; short events; short revents; };
struct timespec { long s, ns; };

/* non-blocking poll: zero timeout */
static long poll0(struct pollfd *p, long n)
{
    struct timespec z = {0, 0};
    return sc6(SYS_ppoll, (long)p, n, (long)&z, 0, 0, 0);
}

void _start(void)
{
    put("[polltest] start\n");

    int fds[2];
    if (sc6(SYS_pipe2, (long)fds, 0, 0, 0, 0, 0) != 0) exit_(61);

    struct pollfd p = {fds[0], POLLIN, 0};
    if (poll0(&p, 1) != 0) exit_(62);          /* nothing ready */
    if (p.revents != 0) exit_(63);

    if (sc6(SYS_write, fds[1], (long)"hi", 2, 0, 0, 0) != 2) exit_(64);
    p.revents = 0;
    if (poll0(&p, 1) != 1) exit_(65);          /* readable now */
    if (!(p.revents & POLLIN)) exit_(66);

    sc6(SYS_close, fds[1], 0, 0, 0, 0, 0);     /* write end gone -> EOF */
    p.revents = 0;
    if (poll0(&p, 1) != 1) exit_(67);
    if (!(p.revents & POLLIN)) exit_(68);
    if (!(p.revents & POLLHUP)) exit_(69);

    struct pollfd o = {1, POLLOUT, 0};         /* stdout always writable */
    if (poll0(&o, 1) != 1) exit_(70);
    if (!(o.revents & POLLOUT)) exit_(71);

    /* pselect6: the same pipe read end is still readable (unread data) */
    {
        struct timespec z = {0, 0};
        unsigned long rf[2] = {0, 0};
        rf[fds[0] >> 6] |= 1UL << (fds[0] & 63);
        long sr = sc6(72 /*pselect6*/, fds[0] + 1, (long)rf, 0, 0, (long)&z, 0);
        if (sr != 1) exit_(72);
        if (!(rf[fds[0] >> 6] & (1UL << (fds[0] & 63)))) exit_(73);

        unsigned long wf[2] = {0, 0};
        wf[0] = 1UL << 1;                        /* stdout */
        sr = sc6(72, 2, 0, (long)wf, 0, (long)&z, 0);
        if (sr != 1) exit_(74);
        if (!(wf[0] & (1UL << 1))) exit_(75);
    }

    sc6(SYS_close, fds[0], 0, 0, 0, 0, 0);
    put("[polltest] ok\n");
    exit_(55);
}
