#include "config.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>

_Atomic(config_t *) g_config;

static config_t cfg_buf[2];
static int      cfg_idx = 0;

void config_init_defaults(void)
{
    cfg_buf[0] = (config_t){ .queue_capacity = 256,
                             .error_threshold = 10,
                             .traffic_threshold_mbps = 10.0 };
    atomic_init(&g_config, &cfg_buf[0]);
}

void config_reload(void)
{
    int next = !cfg_idx;
    config_t *old = atomic_load(&g_config);
    cfg_buf[next] = *old;  /* copy current as baseline */

    const char *v;

    v = getenv("NLAGENT_QUEUE_SIZE");
    if (v) cfg_buf[next].queue_capacity = atoi(v);

    v = getenv("NLAGENT_ERR_THRESHOLD");
    if (v) cfg_buf[next].error_threshold = atoi(v);

    v = getenv("NLAGENT_TRAFFIC_MBPS");
    if (v) cfg_buf[next].traffic_threshold_mbps = atof(v);

    cfg_idx = next;
    atomic_store(&g_config, &cfg_buf[cfg_idx]);

    log_info("config reloaded: queue=%d err=%d traffic=%.1fmbps",
             cfg_buf[cfg_idx].queue_capacity,
             cfg_buf[cfg_idx].error_threshold,
             cfg_buf[cfg_idx].traffic_threshold_mbps);
}
