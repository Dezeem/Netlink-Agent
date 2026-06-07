#define _GNU_SOURCE
#include "prom.h"
#include "logger.h"
#include "parser.h"
#include "runtime_metrics.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <stdio.h>

#define PROMETHEUS_PORT 9100

static int prom_sock = -1;

int prometheus_fd(void) { return prom_sock; }

int prometheus_start(int epoll_fd)
{
    prom_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (prom_sock < 0) {
        log_err("prometheus socket: %s", strerror(errno));
        return -1;
    }

    int opt = 1;
    setsockopt(prom_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = { .sin_family = AF_INET,
                                .sin_port   = htons(PROMETHEUS_PORT),
                                .sin_addr.s_addr = INADDR_ANY };
    if (bind(prom_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_err("prometheus bind: %s", strerror(errno));
        close(prom_sock); prom_sock = -1;
        return -1;
    }
    if (listen(prom_sock, 8) < 0) {
        log_err("prometheus listen: %s", strerror(errno));
        close(prom_sock); prom_sock = -1;
        return -1;
    }

    int flags = fcntl(prom_sock, F_GETFL, 0);
    if (flags >= 0) fcntl(prom_sock, F_SETFL, flags | O_NONBLOCK);

    struct epoll_event ev = { .events = EPOLLIN, .data.fd = prom_sock };
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, prom_sock, &ev) < 0) {
        log_err("prometheus epoll_ctl: %s", strerror(errno));
        close(prom_sock); prom_sock = -1;
        return -1;
    }

    log_info("prometheus exporter listening on :%d", PROMETHEUS_PORT);
    return prom_sock;
}

void prometheus_handle_connection(void)
{
    int conn = accept(prom_sock, NULL, NULL);
    if (conn < 0) return;

    runtime_metrics_snapshot_t snap;
    runtime_metrics_get_snapshot(&snap);

    char buf[2048];
    int len = snprintf(buf, sizeof(buf),
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/plain; version=0.0.4\r\n"
        "\r\n"
        "# HELP nlagent_netlink_events_total Total netlink events received.\n"
        "# TYPE nlagent_netlink_events_total counter\n"
        "nlagent_netlink_events_total %llu\n"
        "# HELP nlagent_worker_events_total Events processed by worker.\n"
        "# TYPE nlagent_worker_events_total counter\n"
        "nlagent_worker_events_total %llu\n"
        "# HELP nlagent_queue_depth Current event queue depth.\n"
        "# TYPE nlagent_queue_depth gauge\n"
        "nlagent_queue_depth %llu\n"
        "# HELP nlagent_queue_depth_max Peak event queue depth.\n"
        "# TYPE nlagent_queue_depth_max gauge\n"
        "nlagent_queue_depth_max %llu\n"
        "# HELP nlagent_dropped_total Events dropped (queue full).\n"
        "# TYPE nlagent_dropped_total counter\n"
        "nlagent_dropped_total %llu\n"
        "# HELP nlagent_errors_total Netlink errors.\n"
        "# TYPE nlagent_errors_total counter\n"
        "nlagent_errors_total %llu\n"
        "# HELP nlagent_overruns_total Netlink buffer overruns.\n"
        "# TYPE nlagent_overruns_total counter\n"
        "nlagent_overruns_total %llu\n"
        "# HELP nlagent_iface_count Current interface count.\n"
        "# TYPE nlagent_iface_count gauge\n"
        "nlagent_iface_count %d\n"
        "# HELP nlagent_cli_connections_active Active CLI connections.\n"
        "# TYPE nlagent_cli_connections_active gauge\n"
        "nlagent_cli_connections_active %llu\n",
        (unsigned long long)snap.netlink_events_total,
        (unsigned long long)snap.worker_events_total,
        (unsigned long long)snap.queue_depth_current,
        (unsigned long long)snap.queue_depth_max,
        (unsigned long long)snap.netlink_dropped_total,
        (unsigned long long)snap.netlink_errors_total,
        (unsigned long long)snap.netlink_overruns_total,
        snap.iface_count,
        (unsigned long long)snap.cli_active_connections);

    (void)!write(conn, buf, (size_t)len);
    close(conn);
}
