#ifndef EVENT_QUEUE_H
#define EVENT_QUEUE_H

#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <linux/netlink.h>

/* Opaque netlink event: nlmsg_type + raw data copy of the nlmsghdr */
typedef struct nl_event {
    uint16_t   nlmsg_type;
    uint32_t   data_len;       /* length of data[] */
    unsigned char data[];      /* copy of nlmsghdr + payload */
} nl_event_t;

/* Power-of-two ring buffer of event pointers */
#define EVENT_QUEUE_CAPACITY 256

typedef struct {
    _Atomic uint64_t  head;    /* producer index */
    _Atomic uint64_t  tail;    /* consumer index */
    uint64_t          mask;    /* CAPACITY - 1 */
    nl_event_t      **slots;   /* array[CAPACITY] */
} event_queue_t;

int          event_queue_init(event_queue_t *q);
void         event_queue_destroy(event_queue_t *q);
bool         event_queue_push(event_queue_t *q, nl_event_t *event);
nl_event_t  *event_queue_pop(event_queue_t *q);
bool         event_queue_full(const event_queue_t *q);

/* Helper: allocate + copy an nl_event from a nlmsghdr */
nl_event_t  *nl_event_from_nlh(struct nlmsghdr *nlh);

#endif
