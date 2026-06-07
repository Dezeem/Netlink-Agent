#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include "logger.h"
#include "parser.h"
#include "metrics.h"
#include "alert.h"
#include "cli.h"
#include "netlink.h"
#include "event_queue.h"
#include "event_worker.h"
#include "config.h"
#include "prom.h"

// declare process_netlink_messages from netlink.c
void process_netlink_messages(void);

static volatile int running = 1;
static int epfd = -1;
static int timer_fd = -1;
static int init_done = 0;

static void sig_handler(int sig)
{
    if (sig == SIGHUP) {
        config_reload();
        log_info("config reloaded via SIGHUP");
        return;
    }
    log_info("received signal %d, exiting...", sig);
    running = 0;
}

static int perform_initialization(void)
{
    config_init_defaults();

    log_info("Starting initialization phase...");
    
    // Phase 1: Basic interface information
    init_iface_table();
    log_info("Basic interface table initialized");
    
    // Phase 2: Complete statistics collection (blocking)
    log_info("Collecting initial statistics...");
    update_all_iface_performance_data();

    log_info("Initial statistics collected");
    init_done = 1;
    log_info("Initialization phase completed");
    return 0;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGHUP,  sig_handler);

    log_info("nlagent starting...");

    if (perform_initialization() < 0) { log_err("Initialization failed"); return 1; }

    epfd = epoll_create1(0);
    if (epfd < 0) { log_err("epoll_create1: %s", strerror(errno)); return 1; }

    /* timerfd: fire every 5s for metrics + alert */
    timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (timer_fd >= 0) {
        struct itimerspec its = {{5, 0}, {5, 0}};
        timerfd_settime(timer_fd, 0, &its, NULL);
        struct epoll_event ev = { .events = EPOLLIN, .data.fd = timer_fd };
        epoll_ctl(epfd, EPOLL_CTL_ADD, timer_fd, &ev);
    }

    if (netlink_start(epfd) < 0) { log_err("netlink_start failed"); return 1; }
    if (cli_start(epfd) < 0) { log_err("cli_start failed"); return 1; }
    if (prometheus_start(epfd) < 0)
        log_warn("prometheus exporter not available");

    event_queue_t eq;
    if (event_queue_init(&eq) < 0) { log_err("event_queue_init failed"); return 1; }
    netlink_set_event_queue(&eq);
    if (event_worker_start(&eq) < 0) { log_err("event_worker_start failed"); return 1; }

    const int MAX_EVENTS = 16;
    struct epoll_event events[MAX_EVENTS];

    while (running) {
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            log_err("epoll_wait: %s", strerror(errno));
            break;
        }
        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;
            if (fd < 0) continue;

            if (fd == netlink_fd()) {
                if (init_done) process_netlink_messages();
            } else if (fd == timer_fd) {
                uint64_t exp;
                (void)!read(timer_fd, &exp, sizeof(exp));  /* drain */
                if (init_done) {
                    metrics_poll_once();
                    alert_check_cycle();
                }
            } else if (fd == prometheus_fd()) {
                prometheus_handle_connection();
            } else {
                cli_handle_connection(fd);
            }
        }
    }

    log_info("nlagent exiting");
    event_worker_stop();
    event_queue_destroy(&eq);
    if (timer_fd >= 0) close(timer_fd);
    close(epfd);
    return 0;
}
