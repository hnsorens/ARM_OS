/*
 * First program linked against the real static musl libc (not -nostdlib).
 * Exercises crt0 -> __libc_start_main -> main, TLS/errno, stdio buffering
 * (fstat/ioctl on stdout + writev), malloc/free (mmap), and open+errno.
 * Prints a line per stage so a hang/crash points at the failing one.
 * Exit 55 = all good.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

int main(void)
{
    write(1, "musltest: start\n", 16);

    printf("musltest: printf %d %s\n", 42, "ok");
    fflush(stdout);

    char *p = malloc(1000);
    if (!p) { write(2, "musltest: malloc failed\n", 24); return 1; }
    memset(p, 'A', 999);
    p[999] = 0;
    printf("musltest: malloc len=%zu first=%c\n", strlen(p), p[0]);
    free(p);

    char *big = malloc(256 * 1024);
    if (!big) { write(2, "musltest: big malloc failed\n", 28); return 2; }
    big[0] = 7;
    big[256 * 1024 - 1] = 9;
    if (big[0] + big[256 * 1024 - 1] != 16) return 3;
    free(big);

    errno = 0;
    int fd = open("/definitely/not/here", O_RDONLY);
    printf("musltest: open ret=%d errno=%d (ENOENT=%d)\n", fd, errno, ENOENT);
    if (fd >= 0 || errno != ENOENT) return 4;

    printf("musltest: pid=%d\n", (int)getpid());

    printf("musltest: done\n");
    return 55;
}
