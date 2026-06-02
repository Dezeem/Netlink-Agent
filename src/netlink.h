#ifndef NETLINK_H
#define NETLINK_H

#include "event_queue.h"

int netlink_start(int epoll_fd);

/* Link the producer to the event queue (called once before the event loop) */
void netlink_set_event_queue(event_queue_t *q);

/* Dispatch a single event – called by the worker thread */
void netlink_dispatch_event(nl_event_t *event);

/* Process incoming netlink messages and push to queue (called by epoll loop) */
void process_netlink_messages(void);
int  netlink_fd(void);

#endif
