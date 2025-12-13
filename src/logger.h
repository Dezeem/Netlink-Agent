#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <stdarg.h>

void log_info(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_err(const char *fmt, ...);

#endif
