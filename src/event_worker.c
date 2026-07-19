#define _GNU_SOURCE
#include "event_worker.h"
#include "logger.h"
#include "runtime_metrics.h"
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <linux/rtnetlink.h>

/* Forward: netlink dispatcher */
void netlink_dispatch_event(nl_event_t *event);

/* Internal state */
static event_queue_t *g_queue  = NULL;
static pthread_t      g_thread = 0;
static volatile int   g_running = 0;

/* Drain every event currently in the queue */
static void worker_drain(event_queue_t *q)
{
    for (;;) {
        nl_event_t *event = event_queue_pop(q);
        if (!event) break;
        if (event->nlmsg_type == 0) {
            free(event);
            continue;
        }
        runtime_metrics_inc_worker_event();
        runtime_metrics_record_queue_depth(event_queue_depth(q));
        netlink_dispatch_event(event);
        free(event);
    }
}

/* Worker main */
static void *worker_main(void *arg)
{
    (void)arg;
    event_queue_t *q = g_queue;
    int efd = event_queue_get_notify_fd(q);

    log_info("event worker thread started");

    while (g_running) {
        /* Drain everything currently queued */
        worker_drain(q);

        if (!g_running) break;

        /* Block until producer signals new data via eventfd */
        uint64_t v;
        ssize_t n = read(efd, &v, sizeof(v));
        if (n != sizeof(v)) {
            if (errno == EINTR) continue;
            log_err("eventfd read failed: %s", strerror(errno));
            break;
        }
    }

    /* Graceful drain – consume everything still in flight */
    log_info("event worker draining remaining events...");
    worker_drain(q);

    log_info("event worker thread exiting");
    return NULL;
}

/* Public API */
int event_worker_start(event_queue_t *q)
{
    g_queue = q;
    g_running = 1;

    if (pthread_create(&g_thread, NULL, worker_main, NULL) != 0) {
        log_err("failed to create event worker thread");
        g_running = 0;
        g_queue = NULL;
        return -1;
    }
    return 0;
}

void event_worker_stop(void)
{
    if (!g_thread) return;

    g_running = 0;

    /* Wake worker if it's blocked on eventfd read */
    {
        int efd = event_queue_get_notify_fd(g_queue);
        uint64_t one = 1;
        (void)!write(efd, &one, sizeof(one));
    }

    pthread_join(g_thread, NULL);
    g_thread = 0;
    g_queue = NULL;
}
