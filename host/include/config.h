#ifndef HHH_CONFIG_H
#define HHH_CONFIG_H

#include <limits.h>
#include <stddef.h>

typedef struct {
    char root_dir[PATH_MAX];
    char libvirt_uri[128];
    char domain_name[128];
    char machine[128];
    char emulator_cpus[128];
    char iothread_cpus[128];
    unsigned int vcpus_per_node;
    unsigned int memory_mib_per_node;
    unsigned int host_node0;
    unsigned int host_node1;
    unsigned int host_node0_first_cpu;
    unsigned int host_node1_first_cpu;
    unsigned int ssh_port;
    unsigned int ssh_timeout_seconds;
    unsigned int shutdown_timeout_seconds;
} Config;

int config_load(const char *path, Config *config, char *error, size_t error_size);

#endif
