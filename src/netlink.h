#ifndef NETLINK_H
#define NETLINK_H

int netlink_start(int epoll_fd);
int netlink_fd(void);

#endif
