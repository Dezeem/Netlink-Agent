#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/signalfd.h>
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

    /* Block signals and deliver them via signalfd as regular epoll events.
     * This eliminates all async-signal-unsafe code (no signal handlers). */
    sigset_t sigmask;
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGINT);
    sigaddset(&sigmask, SIGTERM);
    sigaddset(&sigmask, SIGHUP);
    if (sigprocmask(SIG_BLOCK, &sigmask, NULL) < 0) {
        perror("sigprocmask");
        return 1;
    }

    log_info("nlagent starting...");

    if (perform_initialization() < 0) { log_err("Initialization failed"); return 1; }

    epfd = epoll_create1(0);
    if (epfd < 0) { log_err("epoll_create1: %s", strerror(errno)); return 1; }

    /* signalfd: deliver blocked signals as readable fd events */
    int sigfd = signalfd(-1, &sigmask, SFD_NONBLOCK);
    if (sigfd < 0) { log_err("signalfd: %s", strerror(errno)); return 1; }
    {
        struct epoll_event ev = { .events = EPOLLIN, .data.fd = sigfd };
        epoll_ctl(epfd, EPOLL_CTL_ADD, sigfd, &ev);
    }

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

            if (fd == sigfd) {
                struct signalfd_siginfo si;
                ssize_t n = read(sigfd, &si, sizeof(si));
                if ((size_t)n == sizeof(si)) {
                    if (si.ssi_signo == SIGHUP) {
                        config_reload();
                        log_info("config reloaded via SIGHUP");
                    } else if (si.ssi_signo == SIGINT || si.ssi_signo == SIGTERM) {
                        log_info("received signal %u, exiting...", si.ssi_signo);
                        running = 0;
                    }
                }
            } else if (fd == netlink_fd()) {
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

    /* Phase 1: stop event pipeline */
    event_worker_stop();
    event_queue_destroy(&eq);

    /* Phase 2: close I/O sources (epfd is still alive) */
    netlink_stop();
    cli_stop();
    prometheus_stop();

    /* Phase 3: release data structures */
    cleanup_iface_table();

    /* Phase 4: close infrastructure fds (epfd last) */
    if (timer_fd >= 0) close(timer_fd);
    close(epfd);
    close(sigfd);
    return 0;
}
