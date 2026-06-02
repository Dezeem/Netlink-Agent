#ifndef RUNTIME_METRICS_H
#define RUNTIME_METRICS_H

#include <time.h>
#include <stdint.h>

typedef struct runtime_metrics_snapshot {
    uint64_t netlink_events_total;
    uint64_t link_events_total;
    uint64_t addr_events_total;
    uint64_t route_events_total;
    uint64_t netlink_errors_total;
    uint64_t netlink_overruns_total;
    uint64_t netlink_truncated_total;
    uint64_t netlink_dropped_total;
    time_t last_netlink_event_ts;

    uint64_t cli_connections_total;
    uint64_t cli_active_connections;
    uint64_t cli_commands_total;
    uint64_t cli_errors_total;

    int iface_count;
} runtime_metrics_snapshot_t;

void runtime_metrics_record_netlink_event(int nlmsg_type);
void runtime_metrics_inc_netlink_error(void);
void runtime_metrics_inc_netlink_overrun(void);
void runtime_metrics_inc_netlink_truncated(void);
void runtime_metrics_inc_netlink_dropped(void);

void runtime_metrics_cli_connection_opened(void);
void runtime_metrics_cli_connection_closed(void);
void runtime_metrics_cli_command(void);
void runtime_metrics_cli_error(void);

void runtime_metrics_get_snapshot(runtime_metrics_snapshot_t *snapshot);

#endif
