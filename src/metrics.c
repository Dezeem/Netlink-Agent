#define _GNU_SOURCE
#include "metrics.h"
#include "parser.h"
#include "logger.h"

void metrics_poll_once(void) {
    // Use true SSOT architecture: parser module now handles all data access
    // metrics module simply triggers the data update through parser's unified interface
    update_all_iface_performance_data();
}
