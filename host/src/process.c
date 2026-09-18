#include "process.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

int process_run(char *const argv[], bool quiet)
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        if (quiet) {
            int null_fd = open("/dev/null", O_WRONLY);
            if (null_fd < 0 || dup2(null_fd, STDOUT_FILENO) < 0 ||
                dup2(null_fd, STDERR_FILENO) < 0) {
                _exit(126);
            }
            if (null_fd > STDERR_FILENO) {
                close(null_fd);
            }
        }

        execvp(argv[0], argv);
        _exit(127);
    }

    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            perror("waitpid");
            return -1;
        }
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return -1;
}

void process_sleep(unsigned int seconds)
{
    struct timespec remaining = {
        .tv_sec = (time_t)seconds,
        .tv_nsec = 0,
    };

    while (nanosleep(&remaining, &remaining) < 0 && errno == EINTR) {
    }
}
