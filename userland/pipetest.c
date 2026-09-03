/*
 * Smoke-tests pipe2, per-process cwd (getcwd/chdir + relative openat),
 * and the special /dev nodes. Loaded by the elf_loader `/pipetest`
 * kernelTest. No libc. Exit 66 = pass; 81.. = which check failed.
 */

#define SYS_write 64
#define SYS_read 63
#define SYS_openat 56
#define SYS_close 57
#define SYS_getcwd 17
#define SYS_chdir 49
#define SYS_pipe2 59
#define SYS_mkdirat 34
#define SYS_unlinkat 35
#define SYS_exit 93

#define AT_FDCWD -100
#define AT_REMOVEDIR 0x200
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0100

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
static int streq(const char *a, const char *b) { long i = 0; for (; a[i] || b[i]; i++) if (a[i] != b[i]) return 0; return 1; }

void _start(void)
{
    char buf[64];
    int fds[2];

    /* --- pipe2 --- */
    if (sc6(SYS_pipe2, (long)fds, 0, 0, 0, 0, 0) != 0) exit_(81);
    if (fds[0] < 0 || fds[1] < 0 || fds[0] == fds[1]) exit_(82);
    if (sc6(SYS_write, fds[1], (long)"ping", 4, 0, 0, 0) != 4) exit_(83);
    if (sc6(SYS_read, fds[0], (long)buf, 4, 0, 0, 0) != 4) exit_(84);
    if (buf[0] != 'p' || buf[1] != 'i' || buf[2] != 'n' || buf[3] != 'g') exit_(85);
    sc6(SYS_close, fds[1], 0, 0, 0, 0, 0);
    if (sc6(SYS_read, fds[0], (long)buf, 4, 0, 0, 0) != 0) exit_(86); /* EOF */
    sc6(SYS_close, fds[0], 0, 0, 0, 0, 0);

    /* --- cwd --- */
    if (sc6(SYS_getcwd, (long)buf, sizeof buf, 0, 0, 0, 0) <= 0) exit_(87);
    if (!streq(buf, "/")) exit_(88);
    if (sc6(SYS_mkdirat, AT_FDCWD, (long)"/pipedir", 0755, 0, 0, 0) != 0) exit_(89);
    if (sc6(SYS_chdir, (long)"/pipedir", 0, 0, 0, 0, 0) != 0) exit_(90);
    if (sc6(SYS_getcwd, (long)buf, sizeof buf, 0, 0, 0, 0) <= 0) exit_(91);
    if (!streq(buf, "/pipedir")) exit_(92);
    /* relative openat resolves against the new cwd */
    long rf = sc6(SYS_openat, AT_FDCWD, (long)"relfile", O_CREAT | O_WRONLY, 0644, 0, 0);
    if (rf < 0) exit_(93);
    sc6(SYS_write, rf, (long)"z", 1, 0, 0, 0);
    sc6(SYS_close, rf, 0, 0, 0, 0, 0);
    if (sc6(SYS_chdir, (long)"/", 0, 0, 0, 0, 0) != 0) exit_(94);
    /* the file must now be visible as /pipedir/relfile */
    long af = sc6(SYS_openat, AT_FDCWD, (long)"/pipedir/relfile", O_RDONLY, 0, 0, 0);
    if (af < 0) exit_(95);
    sc6(SYS_close, af, 0, 0, 0, 0, 0);
    sc6(SYS_unlinkat, AT_FDCWD, (long)"/pipedir/relfile", 0, 0, 0, 0);
    if (sc6(SYS_unlinkat, AT_FDCWD, (long)"/pipedir", AT_REMOVEDIR, 0, 0, 0) != 0) exit_(96);

    /* --- /dev nodes --- */
    long dn = sc6(SYS_openat, AT_FDCWD, (long)"/dev/null", O_RDWR, 0, 0, 0);
    if (dn < 0) exit_(97);
    if (sc6(SYS_write, dn, (long)"discard", 7, 0, 0, 0) != 7) exit_(98);
    if (sc6(SYS_read, dn, (long)buf, 8, 0, 0, 0) != 0) exit_(99);
    sc6(SYS_close, dn, 0, 0, 0, 0, 0);

    long dz = sc6(SYS_openat, AT_FDCWD, (long)"/dev/zero", O_RDONLY, 0, 0, 0);
    if (dz < 0) exit_(100);
    for (int i = 0; i < 8; i++) buf[i] = 0xEE;
    if (sc6(SYS_read, dz, (long)buf, 8, 0, 0, 0) != 8) exit_(101);
    for (int i = 0; i < 8; i++) if (buf[i] != 0) exit_(102);
    sc6(SYS_close, dz, 0, 0, 0, 0, 0);

    long du = sc6(SYS_openat, AT_FDCWD, (long)"/dev/urandom", O_RDONLY, 0, 0, 0);
    if (du < 0) exit_(103);
    if (sc6(SYS_read, du, (long)buf, 8, 0, 0, 0) != 8) exit_(104);
    sc6(SYS_close, du, 0, 0, 0, 0, 0);

    put("[pipetest] ok\n");
    exit_(66);
}
