#define _GNU_SOURCE
#include "event_worker.h"
#include "logger.h"
#include "runtime_metrics.h"
#include <stdlib.h>
#include <time.h>
#include <linux/rtnetlink.h>

/* Forward: netlink dispatcher */
void netlink_dispatch_event(nl_event_t *event);

/* Internal state */
static event_queue_t *g_queue  = NULL;
static pthread_t      g_thread = 0;
static volatile int   g_running = 0;

/* Worker main */
static void *worker_main(void *arg)
{
    (void)arg;
    event_queue_t *q = g_queue;
    struct timespec idle = { .tv_sec = 0, .tv_nsec = 100000 }; /* 100 µs */

    log_info("event worker thread started");

    while (g_running) {
        nl_event_t *event = event_queue_pop(q);
        if (event) {
            /* Sentinel type-0 tells us to stop */
            if (event->nlmsg_type == 0) {
                free(event);
                continue;
            }
            netlink_dispatch_event(event);
            free(event);
        } else {
            nanosleep(&idle, NULL);
        }
    }

    /* Graceful drain – consume everything still in flight */
    log_info("event worker draining remaining events...");
    while (1) {
        nl_event_t *event = event_queue_pop(q);
        if (!event) break;
        if (event->nlmsg_type == 0) { free(event); break; }
        netlink_dispatch_event(event);
        free(event);
    }

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

    /* Push sentinel so the worker wakes up if sleeping */
    nl_event_t *sentinel = calloc(1, sizeof(nl_event_t));
    while (sentinel && !event_queue_push(g_queue, sentinel))
        nanosleep(&(struct timespec){0, 100000}, NULL);

    pthread_join(g_thread, NULL);
    g_thread = 0;
    g_queue = NULL;
}
