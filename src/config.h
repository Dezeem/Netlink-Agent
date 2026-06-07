#ifndef CONFIG_H
#define CONFIG_H

#include <stdatomic.h>

typedef struct {
    int    queue_capacity;
    int    error_threshold;
    double traffic_threshold_mbps;
} config_t;

extern _Atomic(config_t *) g_config;

void config_init_defaults(void);
void config_reload(void);

#endif
