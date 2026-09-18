#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum config_key {
    KEY_ROOT_DIR,
    KEY_LIBVIRT_URI,
    KEY_DOMAIN_NAME,
    KEY_MACHINE,
    KEY_VCPUS_PER_NODE,
    KEY_MEMORY_MIB_PER_NODE,
    KEY_HOST_NODE0,
    KEY_HOST_NODE1,
    KEY_HOST_NODE0_FIRST_CPU,
    KEY_HOST_NODE1_FIRST_CPU,
    KEY_EMULATOR_CPUS,
    KEY_IOTHREAD_CPUS,
    KEY_SSH_PORT,
    KEY_SSH_TIMEOUT,
    KEY_SHUTDOWN_TIMEOUT,
    KEY_COUNT
};

static char *trim(char *text)
{
    while (isspace((unsigned char)*text)) {
        ++text;
    }

    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        --end;
    }
    *end = '\0';
    return text;
}

static int copy_text(char *destination, size_t destination_size,
                     const char *value)
{
    int written = snprintf(destination, destination_size, "%s", value);
    return written >= 0 && (size_t)written < destination_size ? 0 : -1;
}

static int parse_unsigned(const char *value, unsigned int *result)
{
    if (*value == '\0' || *value == '-') {
        return -1;
    }

    errno = 0;
    char *end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    if (errno != 0 || *end != '\0' || parsed > 0xffffffffUL) {
        return -1;
    }
    *result = (unsigned int)parsed;
    return 0;
}

static int find_key(const char *name)
{
    static const char *const names[KEY_COUNT] = {
        [KEY_ROOT_DIR] = "root_dir",
        [KEY_LIBVIRT_URI] = "libvirt_uri",
        [KEY_DOMAIN_NAME] = "domain_name",
        [KEY_MACHINE] = "machine",
        [KEY_VCPUS_PER_NODE] = "vcpus_per_node",
        [KEY_MEMORY_MIB_PER_NODE] = "memory_mib_per_node",
        [KEY_HOST_NODE0] = "host_node0",
        [KEY_HOST_NODE1] = "host_node1",
        [KEY_HOST_NODE0_FIRST_CPU] = "host_node0_first_cpu",
        [KEY_HOST_NODE1_FIRST_CPU] = "host_node1_first_cpu",
        [KEY_EMULATOR_CPUS] = "emulator_cpus",
        [KEY_IOTHREAD_CPUS] = "iothread_cpus",
        [KEY_SSH_PORT] = "ssh_port",
        [KEY_SSH_TIMEOUT] = "ssh_timeout_seconds",
        [KEY_SHUTDOWN_TIMEOUT] = "shutdown_timeout_seconds",
    };

    for (int index = 0; index < KEY_COUNT; ++index) {
        if (strcmp(name, names[index]) == 0) {
            return index;
        }
    }
    return -1;
}

static int assign_value(Config *config, int key, const char *value)
{
    switch (key) {
    case KEY_ROOT_DIR:
        return copy_text(config->root_dir, sizeof(config->root_dir), value);
    case KEY_LIBVIRT_URI:
        return copy_text(config->libvirt_uri, sizeof(config->libvirt_uri), value);
    case KEY_DOMAIN_NAME:
        return copy_text(config->domain_name, sizeof(config->domain_name), value);
    case KEY_MACHINE:
        return copy_text(config->machine, sizeof(config->machine), value);
    case KEY_EMULATOR_CPUS:
        return copy_text(config->emulator_cpus, sizeof(config->emulator_cpus), value);
    case KEY_IOTHREAD_CPUS:
        return copy_text(config->iothread_cpus, sizeof(config->iothread_cpus), value);
    case KEY_VCPUS_PER_NODE:
        return parse_unsigned(value, &config->vcpus_per_node);
    case KEY_MEMORY_MIB_PER_NODE:
        return parse_unsigned(value, &config->memory_mib_per_node);
    case KEY_HOST_NODE0:
        return parse_unsigned(value, &config->host_node0);
    case KEY_HOST_NODE1:
        return parse_unsigned(value, &config->host_node1);
    case KEY_HOST_NODE0_FIRST_CPU:
        return parse_unsigned(value, &config->host_node0_first_cpu);
    case KEY_HOST_NODE1_FIRST_CPU:
        return parse_unsigned(value, &config->host_node1_first_cpu);
    case KEY_SSH_PORT:
        return parse_unsigned(value, &config->ssh_port);
    case KEY_SSH_TIMEOUT:
        return parse_unsigned(value, &config->ssh_timeout_seconds);
    case KEY_SHUTDOWN_TIMEOUT:
        return parse_unsigned(value, &config->shutdown_timeout_seconds);
    default:
        return -1;
    }
}

static int validate(const Config *config, char *error, size_t error_size)
{
    if (config->root_dir[0] != '/') {
        snprintf(error, error_size, "root_dir must be an absolute path");
        return -1;
    }
    if (config->vcpus_per_node == 0 || config->vcpus_per_node > 128) {
        snprintf(error, error_size, "vcpus_per_node must be between 1 and 128");
        return -1;
    }
    if (config->memory_mib_per_node == 0) {
        snprintf(error, error_size, "memory_mib_per_node must be greater than zero");
        return -1;
    }
    if (config->host_node0 == config->host_node1) {
        snprintf(error, error_size, "host_node0 and host_node1 must differ");
        return -1;
    }
    if (config->ssh_port == 0 || config->ssh_port > 65535) {
        snprintf(error, error_size, "ssh_port must be between 1 and 65535");
        return -1;
    }
    if (config->ssh_timeout_seconds == 0 ||
        config->shutdown_timeout_seconds == 0) {
        snprintf(error, error_size, "timeouts must be greater than zero");
        return -1;
    }
    return 0;
}

int config_load(const char *path, Config *config, char *error, size_t error_size)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        snprintf(error, error_size, "cannot open %s: %s", path, strerror(errno));
        return -1;
    }

    memset(config, 0, sizeof(*config));
    bool seen[KEY_COUNT] = {false};
    char line[1024];
    unsigned int line_number = 0;
    int result = -1;

    while (fgets(line, sizeof(line), file) != NULL) {
        ++line_number;
        if (strchr(line, '\n') == NULL && !feof(file)) {
            snprintf(error, error_size, "line %u is too long", line_number);
            goto done;
        }

        char *content = trim(line);
        if (*content == '\0' || *content == '#') {
            continue;
        }

        char *separator = strchr(content, '=');
        if (separator == NULL) {
            snprintf(error, error_size, "line %u has no '='", line_number);
            goto done;
        }
        *separator = '\0';
        char *name = trim(content);
        char *value = trim(separator + 1);
        int key = find_key(name);
        if (key < 0) {
            snprintf(error, error_size, "line %u has unknown key '%s'",
                     line_number, name);
            goto done;
        }
        if (seen[key]) {
            snprintf(error, error_size, "line %u repeats key '%s'",
                     line_number, name);
            goto done;
        }
        if (*value == '\0' || assign_value(config, key, value) != 0) {
            snprintf(error, error_size, "line %u has invalid value for '%s'",
                     line_number, name);
            goto done;
        }
        seen[key] = true;
    }

    if (ferror(file)) {
        snprintf(error, error_size, "cannot read %s: %s", path, strerror(errno));
        goto done;
    }

    for (int key = 0; key < KEY_COUNT; ++key) {
        if (!seen[key]) {
            snprintf(error, error_size, "configuration is missing a required key");
            goto done;
        }
    }

    result = validate(config, error, error_size);

done:
    fclose(file);
    return result;
}
