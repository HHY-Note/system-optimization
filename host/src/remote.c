#include "remote.h"

#include "process.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

typedef struct {
    char key[PATH_MAX];
    char known_hosts[PATH_MAX];
    char known_hosts_option[PATH_MAX + 32];
    char port[16];
    char target[64];
} SshConnection;

static int make_path(char *destination, size_t size, const char *root,
                     const char *suffix)
{
    int written = snprintf(destination, size, "%s/%s", root, suffix);
    return written >= 0 && (size_t)written < size ? 0 : -1;
}

static int connection_init(const Config *config, SshConnection *connection)
{
    if (make_path(connection->key, sizeof(connection->key), config->root_dir,
                  "config/vm_ed25519") != 0 ||
        make_path(connection->known_hosts, sizeof(connection->known_hosts),
                  config->root_dir, "config/known_hosts") != 0) {
        fprintf(stderr, "SSH path is too long\n");
        return -1;
    }

    int written = snprintf(connection->known_hosts_option,
                           sizeof(connection->known_hosts_option),
                           "UserKnownHostsFile=%s", connection->known_hosts);
    if (written < 0 ||
        (size_t)written >= sizeof(connection->known_hosts_option)) {
        return -1;
    }
    written = snprintf(connection->port, sizeof(connection->port), "%u",
                       config->ssh_port);
    if (written < 0 || (size_t)written >= sizeof(connection->port)) {
        return -1;
    }
    written = snprintf(connection->target, sizeof(connection->target),
                       "service2@127.0.0.1");
    return written >= 0 && (size_t)written < sizeof(connection->target) ? 0 : -1;
}

static int ssh_run(const Config *config, const char *command, bool quiet)
{
    SshConnection connection;
    if (connection_init(config, &connection) != 0) {
        return -1;
    }

    char *ssh[] = {
        "ssh", "-i", connection.key, "-p", connection.port,
        "-o", "BatchMode=yes",
        "-o", "IdentitiesOnly=yes",
        "-o", "StrictHostKeyChecking=yes",
        "-o", connection.known_hosts_option,
        "-o", "ConnectTimeout=5",
        "-o", "ServerAliveInterval=10",
        "-o", "ServerAliveCountMax=3",
        connection.target, (char *)command, NULL,
    };
    return process_run(ssh, quiet);
}

int remote_wait_ready(const Config *config)
{
    struct timespec start;
    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
        perror("clock_gettime");
        return -1;
    }

    for (;;) {
        if (ssh_run(config, "true", true) == 0) {
            return 0;
        }

        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
            perror("clock_gettime");
            return -1;
        }
        if ((unsigned long)(now.tv_sec - start.tv_sec) >=
            config->ssh_timeout_seconds) {
            fprintf(stderr, "SSH did not become ready within %u seconds\n",
                    config->ssh_timeout_seconds);
            return -1;
        }
        process_sleep(2);
    }
}

static int make_rsync_shell(const Config *config, const SshConnection *connection,
                            char *destination, size_t size)
{
    int written = snprintf(destination, size,
                           "ssh -i %s -p %u -o BatchMode=yes "
                           "-o IdentitiesOnly=yes -o StrictHostKeyChecking=yes "
                           "-o UserKnownHostsFile=%s -o ConnectTimeout=5 "
                           "-o ServerAliveInterval=10 -o ServerAliveCountMax=3",
                           connection->key, config->ssh_port,
                           connection->known_hosts);
    return written >= 0 && (size_t)written < size ? 0 : -1;
}

int remote_copy_source(const Config *config)
{
    SshConnection connection;
    char source[PATH_MAX];
    char rsync_shell[2 * PATH_MAX + 256];
    if (connection_init(config, &connection) != 0 ||
        make_path(source, sizeof(source), config->root_dir, "code/src/") != 0 ||
        make_rsync_shell(config, &connection, rsync_shell,
                         sizeof(rsync_shell)) != 0) {
        fprintf(stderr, "cannot build rsync command\n");
        return -1;
    }

    char *rsync[] = {
        "rsync", "-a", "--timeout=60",
        "--exclude=.build/", "--exclude=output/",
        "-e", rsync_shell, source,
        "service2@127.0.0.1:/home/service2/src/", NULL,
    };
    int status = process_run(rsync, false);
    if (status != 0) {
        fprintf(stderr, "rsync source upload failed with status %d\n", status);
        return -1;
    }
    return 0;
}

int remote_run_phase1(const Config *config)
{
    int status = ssh_run(config, "/home/service2/src/run_phase1", false);
    if (status != 0) {
        fprintf(stderr, "guest phase1 runner failed with status %d\n", status);
        return -1;
    }
    return 0;
}

int remote_fetch_output(const Config *config, const VmRun *run)
{
    SshConnection connection;
    char destination[PATH_MAX];
    char rsync_shell[2 * PATH_MAX + 256];
    int written = snprintf(destination, sizeof(destination), "%s/guest",
                           run->output_path);
    if (written < 0 || (size_t)written >= sizeof(destination) ||
        connection_init(config, &connection) != 0 ||
        make_rsync_shell(config, &connection, rsync_shell,
                         sizeof(rsync_shell)) != 0) {
        fprintf(stderr, "cannot build output transfer command\n");
        return -1;
    }
    if (mkdir(destination, 0700) != 0) {
        fprintf(stderr, "cannot create %s: %s\n", destination, strerror(errno));
        return -1;
    }

    size_t length = strlen(destination);
    if (length + 2 > sizeof(destination)) {
        return -1;
    }
    destination[length] = '/';
    destination[length + 1] = '\0';

    char *rsync[] = {
        "rsync", "-a", "--timeout=60", "-e", rsync_shell,
        "service2@127.0.0.1:/home/service2/src/output/",
        destination, NULL,
    };
    int status = process_run(rsync, false);
    if (status != 0) {
        fprintf(stderr, "rsync output download failed with status %d\n", status);
        return -1;
    }
    return 0;
}

void remote_poweroff(const Config *config)
{
    SshConnection connection;
    if (connection_init(config, &connection) != 0) {
        return;
    }

    char *poweroff[] = {
        "timeout", "15s",
        "ssh", "-i", connection.key, "-p", connection.port,
        "-o", "BatchMode=yes",
        "-o", "IdentitiesOnly=yes",
        "-o", "StrictHostKeyChecking=yes",
        "-o", connection.known_hosts_option,
        "-o", "ConnectTimeout=5",
        connection.target, "sudo -n poweroff", NULL,
    };
    (void)process_run(poweroff, true);
}
