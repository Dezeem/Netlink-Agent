#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/epoll.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include "logger.h"
#include "parser.h"
#include "metrics.h"
#include "alert.h"
#include "cli.h"
#include "netlink.h"

// declare process_netlink_messages from netlink.c
void process_netlink_messages(void);

static int running = 1;
static int epfd = -1;

static void sigint_handler(int sig) {
    log_info("received signal %d, exiting...", sig);
    running = 0;
}

// Flag to track initialization phase
static int initialization_complete = 0;

// Function to perform blocking initialization
static int perform_initialization(void) {
    log_info("Starting initialization phase...");
    
    // Phase 1: Basic interface information
    init_iface_table();
    log_info("Basic interface table initialized");
    
    // Phase 2: Complete statistics collection (blocking)
    log_info("Collecting initial statistics...");
    update_all_iface_performance_data();
    log_info("Initial statistics collected");
    
    // Mark initialization complete
    initialization_complete = 1;
    log_info("Initialization phase completed");
    
    return 0;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    log_info("nlagent starting...");

    // Phase 1: Blocking initialization (no event processing)
    if (perform_initialization() < 0) {
        log_err("Initialization failed");
        return 1;
    }

    // Phase 2: Start event-driven runtime
    epfd = epoll_create1(0);
    if (epfd < 0) {
        log_err("epoll_create1 failed: %s", strerror(errno));
        return 1;
    }

    if (netlink_start(epfd) < 0) {
        log_err("netlink_start failed");
        return 1;
    }
    if (cli_start(epfd) < 0) {
        log_err("cli_start failed");
        return 1;
    }

    const int MAX_EVENTS = 10;
    struct epoll_event events[MAX_EVENTS];

    time_t last_metrics = 0;
    while (running) {
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, 1000); // timeout 1s
        if (nfds < 0) {
            if (errno == EINTR) continue;
            log_err("epoll_wait failed: %s", strerror(errno));
            break;
        }
        for (int i = 0;i<nfds;i++) {
            int fd = events[i].data.fd;
            if (fd == -1) continue;
            if (fd == netlink_fd()) {
                // Only process Netlink messages after initialization
                if (initialization_complete) {
                    process_netlink_messages();
                } else {
                    log_info("Skipping Netlink message during initialization");
                }
            } else if (fd == -1) {
                // skip
            } else {
                // assume cli socket or other
                cli_handle_connection(fd);
            }
        }

        time_t now = time(NULL);
        if (now - last_metrics >= 5) { // poll interval
            metrics_poll_once();
            // Only run alert checks after initialization
            if (initialization_complete) {
                alert_check_cycle();
            }
            last_metrics = now;
        }
    }

    log_info("nlagent exiting");
    close(epfd);
    return 0;
}
