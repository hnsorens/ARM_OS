/*
 * The init process: the first thing the kernel runs at EL0 once module
 * bring-up (and the test suite) has finished. See bootloader/main.zig's
 * startInit(). No libc -- syscalls go straight through `svc #0` (number
 * in x8, args x0..x5, result x0).
 *
 * It prints a banner, then loops: read one byte at a time from fd 0
 * (SYS_read blocks in the keyboard module until a key is pressed),
 * assemble a line, and on Enter echo it back with SYS_write. ^D exits.
 */

static long syscall3(long nr, long a0, long a1, long a2)
{
    register long x8 __asm__("x8") = nr;
    register long x0 __asm__("x0") = a0;
    register long x1 __asm__("x1") = a1;
    register long x2 __asm__("x2") = a2;
    __asm__ volatile("svc #0"
                     : "+r"(x0)
                     : "r"(x8), "r"(x1), "r"(x2)
                     : "memory");
    return x0;
}

#define SYS_read 63
#define SYS_write 64
#define SYS_exit 93

static long slen(const char *s)
{
    long n = 0;
    while (s[n])
        n++;
    return n;
}

static void put(const char *s)
{
    syscall3(SYS_write, 1, (long)s, slen(s));
}

void _start(void)
{
    put("\n=== ARM_OS init (EL0) ===\n");
    put("type a line and press enter; ^D to exit\n> ");

    char line[256];
    int len = 0;

    for (;;) {
        char c;
        long r = syscall3(SYS_read, 0, (long)&c, 1);
        if (r <= 0)
            continue;

        if (c == 4) { /* ^D */
            put("\nbye\n");
            syscall3(SYS_exit, 0, 0, 0);
        }

        if (c == '\n') {
            line[len] = 0;
            put("you said: ");
            syscall3(SYS_write, 1, (long)line, len);
            put("\n> ");
            len = 0;
            continue;
        }

        if (c == 0x7f || c == 8) { /* backspace */
            if (len > 0)
                len--;
            continue;
        }

        if (len < (int)sizeof(line) - 1)
            line[len++] = c;
    }
}
