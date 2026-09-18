#include "config.h"
#include "remote.h"
#include "vm.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    Config config;
    char config_error[256];
    if (config_load("phase1.conf", &config, config_error,
                    sizeof(config_error)) != 0) {
        fprintf(stderr, "configuration error: %s\n", config_error);
        return EXIT_FAILURE;
    }

    VmRun run = {0};
    bool started = false;
    bool ssh_ready = false;
    bool stopped = true;
    bool preserve_runtime = false;
    int result = EXIT_FAILURE;

    puts("[1/6] Creating the per-run overlay and domain XML");
    if (vm_prepare(&config, &run) != 0) {
        goto cleanup;
    }

    puts("[2/6] Starting the Kylin V10 virtual machine");
    if (vm_start(&config, &run) != 0) {
        goto cleanup;
    }
    started = true;
    stopped = false;

    puts("[3/6] Waiting for SSH");
    if (remote_wait_ready(&config) != 0) {
        goto cleanup;
    }
    ssh_ready = true;

    puts("[4/6] Uploading src and running phase 1 in the guest");
    if (remote_copy_source(&config) != 0) {
        goto cleanup;
    }

    int guest_status = remote_run_phase1(&config);

    puts("[5/6] Downloading guest results");
    int fetch_status = remote_fetch_output(&config, &run);
    preserve_runtime = fetch_status != 0;
    if (guest_status != 0 || fetch_status != 0) {
        goto cleanup;
    }
    result = EXIT_SUCCESS;

cleanup:
    if (started) {
        puts("[6/6] Stopping the virtual machine");
        if (ssh_ready) {
            remote_poweroff(&config);
            stopped = vm_wait_stopped(&config,
                                      config.shutdown_timeout_seconds);
        }
        if (!stopped) {
            stopped = vm_force_stop(&config) == 0;
        }
    }

    if (stopped && !preserve_runtime && vm_cleanup(&run) != 0) {
        result = EXIT_FAILURE;
    }
    if (!stopped) {
        fprintf(stderr,
                "domain stop could not be confirmed; runtime files were kept\n");
        result = EXIT_FAILURE;
    }
    if (stopped && preserve_runtime) {
        fprintf(stderr,
                "result download failed; overlay, NVRAM and XML were kept:\n"
                "  %s\n  %s\n  %s\n",
                run.overlay_path, run.vars_path, run.xml_path);
    }

    if (run.output_path[0] != '\0') {
        if (result == EXIT_SUCCESS) {
            printf("Phase 1 completed. Results: %s\n", run.output_path);
        } else {
            fprintf(stderr, "Phase 1 failed. Partial results: %s\n",
                    run.output_path);
        }
    }
    return result;
}
