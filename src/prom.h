#ifndef PROM_H
#define PROM_H

int prometheus_start(int epoll_fd);
void prometheus_handle_connection(void);
int prometheus_fd(void);

#endif
