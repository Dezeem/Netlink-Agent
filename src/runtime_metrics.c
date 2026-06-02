#include "runtime_metrics.h"
#include "parser.h"

#include <pthread.h>
#include <string.h>
#include <linux/rtnetlink.h>

static runtime_metrics_snapshot_t metrics;
static pthread_mutex_t metrics_lock = PTHREAD_MUTEX_INITIALIZER;

static void metrics_lock_acquire(void) {
    pthread_mutex_lock(&metrics_lock);
}

static void metrics_lock_release(void) {
    pthread_mutex_unlock(&metrics_lock);
}

void runtime_metrics_record_netlink_event(int nlmsg_type) {
    time_t now = time(NULL);

    metrics_lock_acquire();
    metrics.netlink_events_total++;
    metrics.last_netlink_event_ts = now;

    switch (nlmsg_type) {
        case RTM_NEWLINK:
        case RTM_DELLINK:
            metrics.link_events_total++;
            break;
        case RTM_NEWADDR:
        case RTM_DELADDR:
            metrics.addr_events_total++;
            break;
        case RTM_NEWROUTE:
        case RTM_DELROUTE:
            metrics.route_events_total++;
            break;
        default:
            break;
    }

    metrics_lock_release();
}

void runtime_metrics_inc_netlink_error(void) {
    metrics_lock_acquire();
    metrics.netlink_errors_total++;
    metrics_lock_release();
}

void runtime_metrics_inc_netlink_overrun(void) {
    metrics_lock_acquire();
    metrics.netlink_overruns_total++;
    metrics_lock_release();
}

void runtime_metrics_inc_netlink_truncated(void) {
    metrics_lock_acquire();
    metrics.netlink_truncated_total++;
    metrics_lock_release();
}

void runtime_metrics_inc_netlink_dropped(void) {
    metrics_lock_acquire();
    metrics.netlink_dropped_total++;
    metrics_lock_release();
}

void runtime_metrics_cli_connection_opened(void) {
    metrics_lock_acquire();
    metrics.cli_connections_total++;
    metrics.cli_active_connections++;
    metrics_lock_release();
}

void runtime_metrics_cli_connection_closed(void) {
    metrics_lock_acquire();
    if (metrics.cli_active_connections > 0) {
        metrics.cli_active_connections--;
    }
    metrics_lock_release();
}

void runtime_metrics_cli_command(void) {
    metrics_lock_acquire();
    metrics.cli_commands_total++;
    metrics_lock_release();
}

void runtime_metrics_cli_error(void) {
    metrics_lock_acquire();
    metrics.cli_errors_total++;
    metrics_lock_release();
}

void runtime_metrics_get_snapshot(runtime_metrics_snapshot_t *snapshot) {
    if (!snapshot) return;

    metrics_lock_acquire();
    memcpy(snapshot, &metrics, sizeof(*snapshot));
    metrics_lock_release();

    snapshot->iface_count = get_iface_count();
}
