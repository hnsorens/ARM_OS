/*
 * Loads the real (prebuilt, static-musl) GNU bash 5.2 and runs a -c
 * script: arithmetic, arrays, brace/param expansion, a pipeline of
 * external commands, command substitution, a function, and exit status.
 * The elf_loader `/bashtest` kernelTest expects exit 42.
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
    static const char msg[] = "[bashtest] exec /bin/bash -c ...\n";
    sc6(SYS_write, 1, (long)msg, sizeof(msg) - 1, 0, 0, 0);

    char *argv[] = {
        "bash", "-c",
        "echo BASH $BASH_VERSION; "
        "a=(one two three); echo ${#a[@]} ${a[1]} ${a[@]: -1}; "
        "n=0; for i in {1..5}; do n=$((n+i)); done; echo sum=$n; "
        "greet() { echo \"hi $1\"; }; greet world; "
        "echo $(echo nested $((3*3))); "
        "printf '%s\\n' alpha beta gamma | sort -r | head -1; "
        "s=abcdef; echo ${s:2:3} ${s^^}; "
        "if [[ $n -eq 15 && -d /bin ]]; then echo cond-ok; fi; "
        "case $s in a*) echo case-ok;; *) echo case-bad;; esac; "
        "cat <<EOF\nheredoc $n\nEOF\n"
        "read x y <<< 'r1 r2'; echo read=$x/$y; "
        "trap 'echo trapped' USR1; kill -USR1 $$; "
        "(sleep 0; echo bg-done) & wait; echo waited; "
        "printf 'l1\\nl2\\nl3\\n' > /bt.tmp; grep l2 /bt.tmp; wc -l < /bt.tmp; rm /bt.tmp; "
        "echo BASH-end; exit 42",
        0,
    };
    char *envp[] = {"PATH=/bin", "HOME=/", "TERM=dumb", 0};
    sc6(SYS_execve, (long)"/bin/bash", (long)argv, (long)envp, 0, 0, 0);

    static const char failed[] = "[bashtest] execve failed\n";
    sc6(SYS_write, 2, (long)failed, sizeof(failed) - 1, 0, 0, 0);
    sc6(SYS_exit, 127, 0, 0, 0, 0, 0);
    for (;;) {
    }
}
