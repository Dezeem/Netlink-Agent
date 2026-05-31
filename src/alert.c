#include "alert.h"
#include "parser.h"
#include "logger.h"
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define ERR_THRESHOLD 10
#define HIGH_RX_RATE_BPS 10000000.0

typedef struct iface_tracker {
    int ifindex;
    char ifname[IFNAMSIZ];
    unsigned long prev_rx;
    unsigned long prev_tx;
    int seen;
    struct iface_tracker *next;
} iface_tracker_t;

static iface_tracker_t *trackers = NULL;
static time_t prev_time = 0;

static iface_tracker_t *find_tracker(int ifindex) {
    for (iface_tracker_t *t = trackers; t; t = t->next) {
        if (t->ifindex == ifindex) return t;
    }
    return NULL;
}

static iface_tracker_t *get_or_create_tracker(iface_info_t *iface) {
    iface_tracker_t *t = find_tracker(iface->ifindex);
    if (t) {
        snprintf(t->ifname, IFNAMSIZ, "%s", iface->ifname);
        return t;
    }

    t = calloc(1, sizeof(*t));
    if (!t) {
        log_warn("failed to allocate alert tracker for %s", iface->ifname);
        return NULL;
    }
    t->ifindex = iface->ifindex;
    snprintf(t->ifname, IFNAMSIZ, "%s", iface->ifname);
    t->prev_rx = iface->rx_bytes;
    t->prev_tx = iface->tx_bytes;
    t->next = trackers;
    trackers = t;
    return t;
}

static void mark_trackers_unseen(void) {
    for (iface_tracker_t *t = trackers; t; t = t->next) {
        t->seen = 0;
    }
}

static void prune_unseen_trackers(void) {
    iface_tracker_t **pp = &trackers;
    while (*pp) {
        iface_tracker_t *t = *pp;
        if (!t->seen) {
            *pp = t->next;
            free(t);
            continue;
        }
        pp = &t->next;
    }
}

static void alert_check_callback(iface_info_t *iface, void *data) {
    time_t now = *(time_t *)data;
    double elapsed = (prev_time == 0) ? 0 : difftime(now, prev_time);
    iface_tracker_t *tracker = get_or_create_tracker(iface);
    if (!tracker) return;

    tracker->seen = 1;

    if (iface->rx_err > ERR_THRESHOLD || iface->tx_err > ERR_THRESHOLD) {
        log_warn("interface %s has rx_err=%lu tx_err=%lu", iface->ifname, iface->rx_err, iface->tx_err);
    }

    if (elapsed > 0) {
        unsigned long rx_diff = iface->rx_bytes >= tracker->prev_rx ? iface->rx_bytes - tracker->prev_rx : 0;
        unsigned long tx_diff = iface->tx_bytes >= tracker->prev_tx ? iface->tx_bytes - tracker->prev_tx : 0;
        double rx_rate = rx_diff / elapsed;
        (void)tx_diff;

        if (rx_rate > HIGH_RX_RATE_BPS) {
            log_warn("high traffic on %s: rx_rate=%.0f B/s", iface->ifname, rx_rate);
        }
    }

    tracker->prev_rx = iface->rx_bytes;
    tracker->prev_tx = iface->tx_bytes;
}

void alert_check_cycle(void) {
    time_t now = time(NULL);

    mark_trackers_unseen();
    foreach_iface(alert_check_callback, &now);
    prune_unseen_trackers();

    prev_time = now;
}
