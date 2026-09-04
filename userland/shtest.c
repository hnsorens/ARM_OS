/*
 * Loads the real (prebuilt, musl-static) BusyBox and runs its ash shell:
 *   /bin/busybox sh -c 'printf "BBSH %d\n" $((6*7)); exit 42'
 * Exercises execve into a 1.25 MB musl binary, its crt/TLS, ash's parser
 * (arithmetic expansion + the printf builtin), and its exit code.
 * The elf_loader `/shtest` kernelTest expects exit 42.
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
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory");
    return x0;
}

#define SYS_write 64
#define SYS_execve 221
#define SYS_exit 93

void _start(void)
{
    static const char msg[] = "[shtest] exec /bin/busybox sh -c ...\n";
    sc6(SYS_write, 1, (long)msg, sizeof(msg) - 1, 0, 0, 0);

    /* A small script exercising loops, arithmetic, variables, pipes
     * between real forked processes, && / ||, redirection + readback,
     * and exit status -- then exit 42 for the kernelTest to assert on. */
    char *argv[] = {
        "sh", "-c",
        "echo BBSH-begin; "
        "for i in 1 2 3; do echo iter=$i; done; "
        "echo hello | rev; "
        "V=$((6*7)); echo V=$V; "
        "echo $V > /shtest.out; cat /shtest.out; rm /shtest.out; "
        "ls /bin | wc -l; "
        "true && echo and-ok; "
        "false || echo or-ok; "
        "test -d /bin && echo bin-is-dir; "
        "echo one two three | tr ' ' '\\n' | sort -r | head -1; "
        "echo BBSH-end; exit 42",
        0,
    };
    char *envp[] = {"PATH=/bin", "HOME=/", 0};
    sc6(SYS_execve, (long)"/bin/busybox", (long)argv, (long)envp, 0, 0, 0);

    static const char failed[] = "[shtest] execve failed\n";
    sc6(SYS_write, 2, (long)failed, sizeof(failed) - 1, 0, 0, 0);
    sc6(SYS_exit, 127, 0, 0, 0, 0, 0);
    for (;;) {
    }
}
