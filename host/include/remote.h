#ifndef HHH_REMOTE_H
#define HHH_REMOTE_H

#include "config.h"
#include "vm.h"

int remote_wait_ready(const Config *config);
int remote_copy_source(const Config *config);
int remote_run_phase1(const Config *config);
int remote_fetch_output(const Config *config, const VmRun *run);
void remote_poweroff(const Config *config);

#endif
