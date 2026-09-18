#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

static int write_summary(void)
{
    struct utsname system_name;
    struct sysinfo memory;
    if (uname(&system_name) != 0 || sysinfo(&memory) != 0) {
        perror("system information");
        return -1;
    }

    FILE *output = fopen("output/summary.txt", "w");
    if (output == NULL) {
        perror("output/summary.txt");
        return -1;
    }

    long online_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    long page_size = sysconf(_SC_PAGESIZE);
    unsigned long long total_memory =
        (unsigned long long)memory.totalram * memory.mem_unit;

    int result = fprintf(output,
                         "sysname=%s\n"
                         "release=%s\n"
                         "machine=%s\n"
                         "online_cpus=%ld\n"
                         "page_size_bytes=%ld\n"
                         "total_memory_bytes=%llu\n",
                         system_name.sysname, system_name.release,
                         system_name.machine, online_cpus, page_size,
                         total_memory) < 0 ? -1 : 0;
    if (fclose(output) != 0) {
        result = -1;
    }
    if (result != 0) {
        fprintf(stderr, "cannot write output/summary.txt\n");
    }
    return result;
}

static int capture(const char *path, char *const argv[])
{
    int output = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (output < 0) {
        fprintf(stderr, "cannot create %s: %s\n", path, strerror(errno));
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(output);
        return -1;
    }
    if (pid == 0) {
        if (dup2(output, STDOUT_FILENO) < 0 ||
            dup2(output, STDERR_FILENO) < 0) {
            _exit(126);
        }
        close(output);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(output);

    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            perror("waitpid");
            return -1;
        }
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "%s failed\n", argv[0]);
        return -1;
    }
    return 0;
}

int main(void)
{
    if (mkdir("output", 0755) != 0 && errno != EEXIST) {
        perror("output");
        return EXIT_FAILURE;
    }

    char *lscpu[] = {"lscpu", NULL};
    char *numa_hardware[] = {"numactl", "--hardware", NULL};
    char *numa_show[] = {"numactl", "--show", NULL};
    char *free_memory[] = {"free", "-h", NULL};

    if (write_summary() != 0 ||
        capture("output/lscpu.txt", lscpu) != 0 ||
        capture("output/numactl-hardware.txt", numa_hardware) != 0 ||
        capture("output/numactl-show.txt", numa_show) != 0 ||
        capture("output/free.txt", free_memory) != 0) {
        return EXIT_FAILURE;
    }

    puts("Guest topology collection completed");
    return EXIT_SUCCESS;
}
