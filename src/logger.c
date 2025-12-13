#include "logger.h"
#include <time.h>

static void vlog(const char *level, const char *fmt, va_list ap) {
    time_t t = time(NULL);
    char ts[64];
    struct tm *tm = localtime(&t);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);
    fprintf(stdout, "[%s] %s: ", ts, level);
    vfprintf(stdout, fmt, ap);
    fprintf(stdout, "\n");
}

void log_info(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vlog("INFO", fmt, ap); va_end(ap);
}
void log_warn(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vlog("WARN", fmt, ap); va_end(ap);
}
void log_err(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vlog("ERROR", fmt, ap); va_end(ap);
}
