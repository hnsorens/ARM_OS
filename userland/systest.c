/*
 * Smoke-tests the batch of "coreutils" syscalls added to fd + elf_loader:
 * uname, getrandom, clock_gettime, writev, mkdirat, openat/O_CREAT, write,
 * ftruncate, fstat, getdents64, unlinkat, getuid. No libc.
 *
 * Exit 55 = all good; 61.. = which check failed.
 */

#define SYS_write 64
#define SYS_writev 66
#define SYS_openat 56
#define SYS_close 57
#define SYS_fstat 80
#define SYS_getdents64 61
#define SYS_mkdirat 34
#define SYS_unlinkat 35
#define SYS_ftruncate 46
#define SYS_uname 160
#define SYS_getrandom 278
#define SYS_clock_gettime 113
#define SYS_getuid 174
#define SYS_exit 93

#define AT_FDCWD -100
#define AT_REMOVEDIR 0x200
#define O_WRONLY 1
#define O_RDONLY 0
#define O_CREAT 0100
#define O_DIRECTORY 0200000

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

struct iovec { void *base; unsigned long len; };

/* aarch64 struct stat: st_size is at byte offset 48 */
static long stat_size(const unsigned char *st) {
    long v = 0;
    for (int i = 0; i < 8; i++) v |= (long)st[48 + i] << (8 * i);
    return v;
}

void _start(void)
{
    unsigned char buf[512];

    /* uname */
    unsigned char uts[390];
    if (sc6(SYS_uname, (long)uts, 0, 0, 0, 0, 0) != 0) exit_(61);
    if (uts[0] != 'L' || uts[1] != 'i' || uts[2] != 'n') exit_(62);

    /* getrandom */
    unsigned char r[16];
    for (int i = 0; i < 16; i++) r[i] = 0;
    if (sc6(SYS_getrandom, (long)r, 16, 0, 0, 0, 0) != 16) exit_(63);
    int any = 0;
    for (int i = 0; i < 16; i++) any |= r[i];
    if (!any) exit_(64);

    /* clock_gettime(CLOCK_MONOTONIC=1) */
    long ts[2];
    if (sc6(SYS_clock_gettime, 1, (long)ts, 0, 0, 0, 0) != 0) exit_(65);

    /* writev */
    struct iovec iov[2] = { { "sys", 3 }, { "test\n", 5 } };
    if (sc6(SYS_writev, 1, (long)iov, 2, 0, 0, 0) != 8) exit_(66);

    /* mkdirat */
    if (sc6(SYS_mkdirat, AT_FDCWD, (long)"/systest_dir", 0755, 0, 0, 0) != 0) exit_(67);

    /* create + write + ftruncate + fstat */
    long fd = sc6(SYS_openat, AT_FDCWD, (long)"/systest_dir/f", O_CREAT | O_WRONLY, 0644, 0, 0);
    if (fd < 0) exit_(68);
    if (sc6(SYS_write, fd, (long)"hello", 5, 0, 0, 0) != 5) exit_(69);
    if (sc6(SYS_ftruncate, fd, 3, 0, 0, 0, 0) != 0) exit_(70);
    unsigned char st[128];
    if (sc6(SYS_fstat, fd, (long)st, 0, 0, 0, 0) != 0) exit_(71);
    if (stat_size(st) != 3) exit_(72);
    sc6(SYS_close, fd, 0, 0, 0, 0, 0);

    /* getdents64 */
    long dfd = sc6(SYS_openat, AT_FDCWD, (long)"/systest_dir", O_RDONLY | O_DIRECTORY, 0, 0, 0);
    if (dfd < 0) exit_(73);
    long got = sc6(SYS_getdents64, dfd, (long)buf, sizeof buf, 0, 0, 0);
    if (got <= 0) exit_(74);
    /* find an entry named "f" */
    int found_f = 0;
    long off = 0;
    while (off < got) {
        unsigned short reclen = buf[off + 16] | (buf[off + 17] << 8);
        const char *name = (const char *)&buf[off + 19];
        if (name[0] == 'f' && name[1] == 0) found_f = 1;
        if (reclen == 0) break;
        off += reclen;
    }
    if (!found_f) exit_(75);
    sc6(SYS_close, dfd, 0, 0, 0, 0, 0);

    /* cleanup */
    if (sc6(SYS_unlinkat, AT_FDCWD, (long)"/systest_dir/f", 0, 0, 0, 0) != 0) exit_(76);
    if (sc6(SYS_unlinkat, AT_FDCWD, (long)"/systest_dir", AT_REMOVEDIR, 0, 0, 0) != 0) exit_(77);

    if (sc6(SYS_getuid, 0, 0, 0, 0, 0, 0) != 0) exit_(78);

    put("[systest] ok\n");
    exit_(55);
}
