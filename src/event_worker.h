#ifndef EVENT_WORKER_H
#define EVENT_WORKER_H

#include <pthread.h>
#include "event_queue.h"

int  event_worker_start(event_queue_t *q);
void event_worker_stop(void);

#endif
