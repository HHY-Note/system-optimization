#include "vm.h"

#include "process.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static int make_path(char *destination, size_t size, const char *root,
                     const char *suffix)
{
    int written = snprintf(destination, size, "%s/%s", root, suffix);
    if (written < 0 || (size_t)written >= size) {
        fprintf(stderr, "path is too long: %s/%s\n", root, suffix);
        return -1;
    }
    return 0;
}

static int copy_file(const char *source, const char *destination)
{
    int input = open(source, O_RDONLY);
    if (input < 0) {
        fprintf(stderr, "cannot open %s: %s\n", source, strerror(errno));
        return -1;
    }

    int output = open(destination, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (output < 0) {
        fprintf(stderr, "cannot create %s: %s\n", destination, strerror(errno));
        close(input);
        return -1;
    }

    char buffer[64 * 1024];
    int result = 0;
    for (;;) {
        ssize_t count = read(input, buffer, sizeof(buffer));
        if (count == 0) {
            break;
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "cannot read %s: %s\n", source, strerror(errno));
            result = -1;
            break;
        }

        ssize_t offset = 0;
        while (offset < count) {
            ssize_t written = write(output, buffer + offset,
                                    (size_t)(count - offset));
            if (written < 0) {
                if (errno == EINTR) {
                    continue;
                }
                fprintf(stderr, "cannot write %s: %s\n", destination,
                        strerror(errno));
                result = -1;
                break;
            }
            offset += written;
        }
        if (result != 0) {
            break;
        }
    }

    if (close(input) != 0) {
        result = -1;
    }
    if (close(output) != 0) {
        fprintf(stderr, "cannot close %s: %s\n", destination, strerror(errno));
        result = -1;
    }
    if (result != 0) {
        unlink(destination);
    }
    return result;
}

static int write_xml(const Config *config, const VmRun *run,
                     const char *firmware_code)
{
    FILE *xml = fopen(run->xml_path, "wx");
    if (xml == NULL) {
        fprintf(stderr, "cannot create %s: %s\n", run->xml_path,
                strerror(errno));
        return -1;
    }

    const unsigned int total_vcpus = 2U * config->vcpus_per_node;
    const unsigned int total_memory = 2U * config->memory_mib_per_node;
    const unsigned int second_guest_cpu = config->vcpus_per_node;
    const unsigned int last_guest_cpu = total_vcpus - 1U;

    int error = 0;
    if (fprintf(xml,
                "<domain type='kvm' xmlns:qemu='http://libvirt.org/schemas/domain/qemu/1.0'>\n"
                "  <name>%s</name>\n"
                "  <memory unit='MiB'>%u</memory>\n"
                "  <vcpu placement='static'>%u</vcpu>\n"
                "  <iothreads>1</iothreads>\n"
                "  <cputune>\n",
                config->domain_name, total_memory, total_vcpus) < 0) {
        error = -1;
    }

    for (unsigned int vcpu = 0; error == 0 && vcpu < total_vcpus; ++vcpu) {
        unsigned int host_cpu;
        if (vcpu < config->vcpus_per_node) {
            host_cpu = config->host_node0_first_cpu + vcpu;
        } else {
            host_cpu = config->host_node1_first_cpu +
                       (vcpu - config->vcpus_per_node);
        }
        if (fprintf(xml, "    <vcpupin vcpu='%u' cpuset='%u'/>\n", vcpu,
                    host_cpu) < 0) {
            error = -1;
        }
    }

    if (error == 0 &&
        fprintf(xml,
                "    <emulatorpin cpuset='%s'/>\n"
                "    <iothreadpin iothread='1' cpuset='%s'/>\n"
                "  </cputune>\n"
                "  <numatune>\n"
                "    <memory mode='strict' nodeset='%u,%u'/>\n"
                "    <memnode cellid='0' mode='strict' nodeset='%u'/>\n"
                "    <memnode cellid='1' mode='strict' nodeset='%u'/>\n"
                "  </numatune>\n"
                "  <os>\n"
                "    <type arch='aarch64' machine='%s'>hvm</type>\n"
                "    <loader readonly='yes' type='pflash'>%s</loader>\n"
                "    <nvram>%s</nvram>\n"
                "  </os>\n"
                "  <features>\n"
                "    <acpi/>\n"
                "    <gic version='3'/>\n"
                "  </features>\n"
                "  <cpu mode='host-passthrough' check='none'>\n"
                "    <topology sockets='2' cores='%u' threads='1'/>\n"
                "    <numa>\n"
                "      <cell id='0' cpus='0-%u' memory='%u' unit='MiB'/>\n"
                "      <cell id='1' cpus='%u-%u' memory='%u' unit='MiB'/>\n"
                "    </numa>\n"
                "  </cpu>\n"
                "  <clock offset='utc'/>\n"
                "  <on_poweroff>destroy</on_poweroff>\n"
                "  <on_reboot>restart</on_reboot>\n"
                "  <on_crash>destroy</on_crash>\n"
                "  <devices>\n"
                "    <emulator>/usr/bin/qemu-system-aarch64</emulator>\n"
                "    <controller type='pci' index='0' model='pcie-root'/>\n"
                "    <disk type='file' device='disk'>\n"
                "      <driver name='qemu' type='qcow2' cache='none' iothread='1'/>\n"
                "      <source file='%s'/>\n"
                "      <target dev='vda' bus='virtio'/>\n"
                "      <boot order='1'/>\n"
                "      <address type='pci' domain='0x0000' bus='0x00' slot='0x01' function='0x0'/>\n"
                "    </disk>\n"
                "    <interface type='user'>\n"
                "      <mac address='52:54:00:48:48:01'/>\n"
                "      <model type='virtio'/>\n"
                "      <address type='pci' domain='0x0000' bus='0x00' slot='0x03' function='0x0'/>\n"
                "    </interface>\n"
                "    <rng model='virtio'>\n"
                "      <backend model='random'>/dev/urandom</backend>\n"
                "    </rng>\n"
                "    <memballoon model='none'/>\n"
                "  </devices>\n"
                "  <qemu:commandline>\n"
                "    <qemu:arg value='-set'/>\n"
                "    <qemu:arg value='netdev.hostnet0.hostfwd=tcp:127.0.0.1:%u-:22'/>\n"
                "  </qemu:commandline>\n"
                "</domain>\n",
                config->emulator_cpus, config->iothread_cpus,
                config->host_node0, config->host_node1,
                config->host_node0, config->host_node1,
                config->machine, firmware_code, run->vars_path,
                config->vcpus_per_node, config->vcpus_per_node - 1U,
                config->memory_mib_per_node, second_guest_cpu,
                last_guest_cpu, config->memory_mib_per_node,
                run->overlay_path, config->ssh_port) < 0) {
        error = -1;
    }

    if (fclose(xml) != 0) {
        error = -1;
    }
    if (error != 0) {
        fprintf(stderr, "cannot write %s\n", run->xml_path);
        unlink(run->xml_path);
    }
    return error;
}

int vm_prepare(const Config *config, VmRun *run)
{
    memset(run, 0, sizeof(*run));

    time_t now = time(NULL);
    struct tm local_time;
    if (now == (time_t)-1 || localtime_r(&now, &local_time) == NULL) {
        fprintf(stderr, "cannot create run timestamp\n");
        return -1;
    }

    char timestamp[32];
    if (strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", &local_time) == 0) {
        fprintf(stderr, "cannot format run timestamp\n");
        return -1;
    }

    char suffix[256];
    int written = snprintf(suffix, sizeof(suffix), "output/%s-phase1-%ld",
                           timestamp, (long)getpid());
    if (written < 0 || (size_t)written >= sizeof(suffix) ||
        make_path(run->output_path, sizeof(run->output_path),
                  config->root_dir, suffix) != 0) {
        return -1;
    }
    if (mkdir(run->output_path, 0700) != 0) {
        fprintf(stderr, "cannot create %s: %s\n", run->output_path,
                strerror(errno));
        return -1;
    }

    written = snprintf(suffix, sizeof(suffix), "overlay/phase1-%ld.qcow2",
                       (long)getpid());
    if (written < 0 || (size_t)written >= sizeof(suffix) ||
        make_path(run->overlay_path, sizeof(run->overlay_path),
                  config->root_dir, suffix) != 0) {
        return -1;
    }
    written = snprintf(suffix, sizeof(suffix), "overlay/phase1-vars-%ld.fd",
                       (long)getpid());
    if (written < 0 || (size_t)written >= sizeof(suffix) ||
        make_path(run->vars_path, sizeof(run->vars_path),
                  config->root_dir, suffix) != 0) {
        return -1;
    }
    written = snprintf(suffix, sizeof(suffix), "vm/phase1-%ld.xml",
                       (long)getpid());
    if (written < 0 || (size_t)written >= sizeof(suffix) ||
        make_path(run->xml_path, sizeof(run->xml_path),
                  config->root_dir, suffix) != 0) {
        return -1;
    }

    char base_image[PATH_MAX];
    char firmware_code[PATH_MAX];
    char firmware_vars[PATH_MAX];
    if (make_path(base_image, sizeof(base_image), config->root_dir,
                  "image/kylin-v10-base.qcow2") != 0 ||
        make_path(firmware_code, sizeof(firmware_code), config->root_dir,
                  "image/AAVMF_CODE.fd") != 0 ||
        make_path(firmware_vars, sizeof(firmware_vars), config->root_dir,
                  "image/AAVMF_VARS.base.fd") != 0) {
        return -1;
    }

    char *qemu_img[] = {
        "qemu-img", "create", "-f", "qcow2", "-F", "qcow2",
        "-b", base_image, run->overlay_path, NULL,
    };
    if (process_run(qemu_img, false) != 0) {
        fprintf(stderr, "qemu-img failed\n");
        return -1;
    }
    run->overlay_created = true;

    if (copy_file(firmware_vars, run->vars_path) != 0) {
        return -1;
    }
    run->vars_created = true;

    if (write_xml(config, run, firmware_code) != 0) {
        return -1;
    }
    run->xml_created = true;
    return 0;
}

int vm_start(const Config *config, const VmRun *run)
{
    char *virsh[] = {
        "virsh", "-c", (char *)config->libvirt_uri,
        "create", (char *)run->xml_path, NULL,
    };
    int status = process_run(virsh, false);
    if (status != 0) {
        fprintf(stderr, "virsh create failed with status %d\n", status);
        return -1;
    }
    return 0;
}

static bool vm_is_running(const Config *config)
{
    char *virsh[] = {
        "virsh", "-c", (char *)config->libvirt_uri,
        "domstate", (char *)config->domain_name, NULL,
    };
    return process_run(virsh, true) == 0;
}

bool vm_wait_stopped(const Config *config, unsigned int timeout_seconds)
{
    for (unsigned int elapsed = 0; elapsed < timeout_seconds; ++elapsed) {
        if (!vm_is_running(config)) {
            return true;
        }
        process_sleep(1);
    }
    return !vm_is_running(config);
}

int vm_force_stop(const Config *config)
{
    if (!vm_is_running(config)) {
        return 0;
    }

    char *virsh[] = {
        "virsh", "-c", (char *)config->libvirt_uri,
        "destroy", (char *)config->domain_name, NULL,
    };
    if (process_run(virsh, false) != 0) {
        fprintf(stderr, "cannot destroy domain %s\n", config->domain_name);
        return -1;
    }
    return vm_wait_stopped(config, 10) ? 0 : -1;
}

int vm_cleanup(VmRun *run)
{
    int result = 0;
    if (run->xml_created && unlink(run->xml_path) != 0) {
        fprintf(stderr, "cannot remove %s: %s\n", run->xml_path,
                strerror(errno));
        result = -1;
    }
    if (run->vars_created && unlink(run->vars_path) != 0) {
        fprintf(stderr, "cannot remove %s: %s\n", run->vars_path,
                strerror(errno));
        result = -1;
    }
    if (run->overlay_created && unlink(run->overlay_path) != 0) {
        fprintf(stderr, "cannot remove %s: %s\n", run->overlay_path,
                strerror(errno));
        result = -1;
    }
    return result;
}
