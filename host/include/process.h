#ifndef HHH_PROCESS_H
#define HHH_PROCESS_H

#include <stdbool.h>

int process_run(char *const argv[], bool quiet);
void process_sleep(unsigned int seconds);

#endif
