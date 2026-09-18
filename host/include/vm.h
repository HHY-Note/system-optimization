#ifndef HHH_VM_H
#define HHH_VM_H

#include "config.h"

#include <limits.h>
#include <stdbool.h>

typedef struct {
    char overlay_path[PATH_MAX];
    char vars_path[PATH_MAX];
    char xml_path[PATH_MAX];
    char output_path[PATH_MAX];
    bool overlay_created;
    bool vars_created;
    bool xml_created;
} VmRun;

int vm_prepare(const Config *config, VmRun *run);
int vm_start(const Config *config, const VmRun *run);
bool vm_wait_stopped(const Config *config, unsigned int timeout_seconds);
int vm_force_stop(const Config *config);
int vm_cleanup(VmRun *run);

#endif
