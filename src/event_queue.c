#include "event_queue.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <linux/netlink.h>

/* Queue management */
int event_queue_init(event_queue_t *q)
{
    q->slots = calloc(EVENT_QUEUE_CAPACITY, sizeof(nl_event_t *));
    if (!q->slots) {
        log_err("failed to allocate event queue slots");
        return -1;
    }
    atomic_init(&q->head, 0);
    atomic_init(&q->tail, 0);
    q->mask = EVENT_QUEUE_CAPACITY - 1;
    return 0;
}

void event_queue_destroy(event_queue_t *q)
{
    /* Free any events still in the queue */
    while (1) {
        nl_event_t *e = event_queue_pop(q);
        if (!e) break;
        free(e);
    }
    free(q->slots);
    q->slots = NULL;
}

/* SPSC producer */
bool event_queue_push(event_queue_t *q, nl_event_t *event)
{
    uint64_t head = atomic_load_explicit(&q->head, memory_order_relaxed);
    uint64_t tail = atomic_load_explicit(&q->tail, memory_order_acquire);

    if (head - tail >= EVENT_QUEUE_CAPACITY)
        return false;

    q->slots[head & q->mask] = event;
    atomic_store_explicit(&q->head, head + 1, memory_order_release);
    return true;
}

/* SPSC consumer */
nl_event_t *event_queue_pop(event_queue_t *q)
{
    uint64_t tail = atomic_load_explicit(&q->tail, memory_order_relaxed);
    uint64_t head = atomic_load_explicit(&q->head, memory_order_acquire);

    if (tail == head)
        return NULL;

    nl_event_t *event = q->slots[tail & q->mask];
    atomic_store_explicit(&q->tail, tail + 1, memory_order_release);
    return event;
}

/* Query */
bool event_queue_full(const event_queue_t *q)
{
    uint64_t head = atomic_load_explicit(&q->head, memory_order_relaxed);
    uint64_t tail = atomic_load_explicit(&q->tail, memory_order_acquire);
    return (head - tail) >= EVENT_QUEUE_CAPACITY;
}

uint64_t event_queue_depth(const event_queue_t *q)
{
    uint64_t head = atomic_load_explicit(&q->head, memory_order_acquire);
    uint64_t tail = atomic_load_explicit(&q->tail, memory_order_acquire);
    return head - tail;
}

/* nl_event factory */
nl_event_t *nl_event_from_nlh(struct nlmsghdr *nlh)
{
    size_t data_len = nlh->nlmsg_len;
    nl_event_t *event = malloc(sizeof(nl_event_t) + data_len);
    if (!event) {
        log_err("failed to allocate nl_event (len=%zu)", data_len);
        return NULL;
    }
    event->nlmsg_type = nlh->nlmsg_type;
    event->data_len = (uint32_t)data_len;
    memcpy(event->data, nlh, data_len);
    return event;
}
