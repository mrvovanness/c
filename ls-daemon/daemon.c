#include "daemon.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int daemonize(void)
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid > 0)
        _exit(EXIT_SUCCESS);

    if (setsid() < 0) {
        perror("setsid");
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid > 0)
        _exit(EXIT_SUCCESS);

    int devnull = open("/dev/null", O_RDWR);
    if (devnull < 0) {
        perror("open /dev/null");
        return -1;
    }

    if (dup2(devnull, STDIN_FILENO) < 0 || dup2(devnull, STDOUT_FILENO) < 0 ||
        dup2(devnull, STDERR_FILENO) < 0) {
        perror("dup2");
        close(devnull);
        return -1;
    }

    if (devnull > STDERR_FILENO)
        close(devnull);

    return 0;
}
