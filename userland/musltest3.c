/*
 * Shell-pipeline machinery through real musl: pipe + fork + dup2 + execve,
 * so a child's stdout lands in a pipe the parent reads back; plus passing
 * a custom environment across execve and reading it in the child via a
 * re-exec of this same program. Exit 55 = pass.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

extern char **environ;

int main(int argc, char **argv)
{
    /* --- re-exec leg: if invoked with argv[1]=="child", just report env --- */
    if (argc >= 2 && strcmp(argv[1], "child") == 0) {
        const char *v = getenv("ARMOS_TEST");
        write(1, v ? v : "(null)", v ? strlen(v) : 6);
        return 0;
    }

    write(1, "musltest3: start\n", 17);

    /* --- pipeline: /hello's stdout -> pipe -> us --- */
    int pf[2];
    if (pipe(pf) != 0) return 20;
    pid_t k = fork();
    if (k < 0) return 21;
    if (k == 0) {
        dup2(pf[1], 1);          /* stdout -> pipe write end */
        close(pf[0]);
        close(pf[1]);
        char *av[] = {"/hello", NULL};
        execve("/hello", av, environ);
        _exit(127);
    }
    close(pf[1]);
    char rb[64] = {0};
    int total = 0, n;
    while ((n = read(pf[0], rb + total, sizeof rb - 1 - total)) > 0) total += n;
    close(pf[0]);
    int st = 0;
    waitpid(k, &st, 0);
    if (strncmp(rb, "hello from EL0\n", 14) != 0) {
        printf("musltest3: piped [%s]\n", rb);
        return 22;
    }
    printf("musltest3: pipeline ok (%d bytes)\n", total);

    /* --- custom environment across execve, into a re-exec of ourselves --- */
    int ef[2];
    if (pipe(ef) != 0) return 30;
    pid_t k2 = fork();
    if (k2 < 0) return 31;
    if (k2 == 0) {
        dup2(ef[1], 1);
        close(ef[0]);
        close(ef[1]);
        char *av[] = {argv[0], "child", NULL};
        char *ev[] = {"ARMOS_TEST=piped-env-42", NULL};
        execve(argv[0], av, ev);
        _exit(127);
    }
    close(ef[1]);
    char eb[64] = {0};
    total = 0;
    while ((n = read(ef[0], eb + total, sizeof eb - 1 - total)) > 0) total += n;
    close(ef[0]);
    waitpid(k2, &st, 0);
    if (strcmp(eb, "piped-env-42") != 0) {
        printf("musltest3: env got [%s]\n", eb);
        return 32;
    }
    printf("musltest3: env-across-execve ok\n");

    printf("musltest3: done\n");
    return 55;
}
