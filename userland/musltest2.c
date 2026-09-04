/*
 * Second real-musl program: the surface a coreutil / shell actually hits.
 * fork + execve + waitpid through libc, sigaction + kill + catch,
 * opendir/readdir (getdents64), buffered file I/O on the ext2 rootfs,
 * and a few pure-libc helpers. Exit 55 = all good; other codes point at
 * the failed stage.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>
#include <sys/wait.h>
#include <sys/stat.h>

static volatile sig_atomic_t got_usr1 = 0;
static void on_usr1(int s) { (void)s; got_usr1 = 1; }

static int cmp_int(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;
}

int main(void)
{
    /* --- pure libc helpers --- */
    char buf[64];
    int n = snprintf(buf, sizeof buf, "%d-%s-%x", 7, "z", 255);
    if (n != 6 || strcmp(buf, "7-z-ff") != 0) return 10;
    if (strtol("  -123abc", NULL, 10) != -123) return 11;
    int arr[5] = {4, 1, 3, 5, 2};
    qsort(arr, 5, sizeof(int), cmp_int);
    if (arr[0] != 1 || arr[4] != 5) return 12;
    printf("musltest2: helpers ok (%s)\n", buf);

    /* --- signals: sigaction + kill + self-catch --- */
    struct sigaction sa = {0};
    sa.sa_handler = on_usr1;
    if (sigaction(SIGUSR1, &sa, NULL) != 0) return 20;
    if (kill(getpid(), SIGUSR1) != 0) return 21;
    if (!got_usr1) return 22;
    printf("musltest2: signal caught\n");

    /* --- buffered file I/O on the ext2 rootfs --- */
    FILE *f = fopen("/musltest2.tmp", "w");
    if (!f) { printf("fopen w failed errno=%d\n", errno); return 30; }
    if (fprintf(f, "line %d\n", 1) < 0) return 31;
    if (fwrite("second\n", 1, 7, f) != 7) return 32;
    if (fclose(f) != 0) return 33;

    f = fopen("/musltest2.tmp", "r");
    if (!f) return 34;
    char l1[32] = {0};
    if (!fgets(l1, sizeof l1, f)) return 35;
    if (strcmp(l1, "line 1\n") != 0) { printf("got [%s]\n", l1); return 36; }
    int total = 0, c;
    while ((c = fgetc(f)) != EOF) total++;
    if (total != 7) return 37;
    fclose(f);
    struct stat st;
    if (stat("/musltest2.tmp", &st) != 0 || st.st_size != 14) return 38;
    unlink("/musltest2.tmp");
    printf("musltest2: file io ok\n");

    /* --- opendir / readdir --- */
    DIR *d = opendir("/");
    if (!d) return 40;
    int saw_hello = 0, entries = 0;
    struct dirent *de;
    while ((de = readdir(d))) {
        entries++;
        if (strcmp(de->d_name, "hello") == 0) saw_hello = 1;
    }
    closedir(d);
    if (!saw_hello || entries < 3) { printf("entries=%d hello=%d\n", entries, saw_hello); return 41; }
    printf("musltest2: readdir ok (%d entries)\n", entries);

    /* --- fork + execve + waitpid --- */
    pid_t kid = fork();
    if (kid < 0) return 50;
    if (kid == 0) {
        char *av[] = {"/hello", NULL};
        execve("/hello", av, environ);
        _exit(127); /* only reached if execve failed */
    }
    int status = 0;
    if (waitpid(kid, &status, 0) != kid) return 51;
    if (!WIFEXITED(status)) return 52;
    if (WEXITSTATUS(status) != ((100 + kid) & 0xff)) {
        printf("child status=%d want=%d\n", WEXITSTATUS(status), (100 + kid) & 0xff);
        return 53;
    }
    printf("musltest2: fork+exec+wait ok (child %d)\n", (int)kid);

    printf("musltest2: done\n");
    return 55;
}
