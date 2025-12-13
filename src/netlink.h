#ifndef NETLINK_H
#define NETLINK_H

int netlink_start(int epoll_fd);
int netlink_fd(void);

/* process incoming messages (to be called by main loop when nl fd is readable) */
void process_netlink_messages(void);

#endif
