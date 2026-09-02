/*
 * The first EL0 program. No libc: syscalls go straight through `svc #0`
 * with the AArch64 Linux convention (number in x8, args x0..x5, result in
 * x0). Built static, non-PIE, linked at 64 GiB (well above the kernel's
 * identity-mapped range) -- see build.zig and modules/hnsorens/proc/elf_loader.
 *
 * It writes a line to fd 1, reads its own pid, and exits with 100 + pid,
 * so the elf_loader kernelTest can confirm the whole path end to end:
 * ELF load, drop to EL0, syscalls from EL0, and pid plumbing.
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

#define SYS_write 64
#define SYS_getpid 172
#define SYS_exit 93

void _start(void)
{
    static const char msg[] = "hello from EL0\n";
    syscall3(SYS_write, 1, (long)msg, sizeof(msg) - 1);

    long pid = syscall3(SYS_getpid, 0, 0, 0);
    syscall3(SYS_exit, 100 + pid, 0, 0);

    for (;;) {
    }
}
