#define _GNU_SOURCE
#include "netlink.h"
#include "parser.h"
#include "logger.h"

#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

static int nl_sock = -1;

int netlink_fd(void) { return nl_sock; }

static void handle_link_msg(struct nlmsghdr *nlh) {
    struct ifinfomsg *ifi = NLMSG_DATA(nlh);
    int ifindex = ifi->ifi_index;
    int is_up = (ifi->ifi_flags & IFF_RUNNING) ? 1 : 0;
    update_iface_status(ifindex, is_up);
}

static void handle_addr_msg(struct nlmsghdr *nlh) {
    // for simplicity not parsing addresses in detail
    // can be extended to parse ifaddrmsg
    struct ifaddrmsg *ifa = NLMSG_DATA(nlh);
    int ifindex = ifa->ifa_index;
    log_info("addr change on ifindex %d", ifindex);
}

int netlink_start(int epoll_fd) {
    nl_sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (nl_sock < 0) {
        log_err("socket NETLINK_ROUTE failed: %s", strerror(errno));
        return -1;
    }
    struct sockaddr_nl sa;
    memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;
    sa.nl_pid = getpid();
    sa.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR | RTMGRP_IPV4_ROUTE;

    if (bind(nl_sock, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
        log_err("bind netlink failed: %s", strerror(errno));
        close(nl_sock);
        return -1;
    }

    // add to epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = nl_sock;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, nl_sock, &ev) < 0) {
        log_err("epoll_ctl add nl_sock failed: %s", strerror(errno));
        close(nl_sock);
        return -1;
    }
    log_info("netlink socket started (fd=%d)", nl_sock);
    return nl_sock;
}

void process_netlink_messages(void) {
    char buf[4096];
    struct iovec iov = { buf, sizeof(buf) };
    struct sockaddr_nl sa;
    struct msghdr msg = { (void*)&sa, sizeof(sa), &iov, 1, NULL, 0, 0 };
    ssize_t len = recvmsg(nl_sock, &msg, 0);
    if (len < 0) {
        log_err("recvmsg nl_sock failed: %s", strerror(errno));
        return;
    }
    for (struct nlmsghdr *nlh = (struct nlmsghdr*)buf; NLMSG_OK(nlh, (unsigned int)len); nlh = NLMSG_NEXT(nlh, len)) {
        switch (nlh->nlmsg_type) {
            case RTM_NEWLINK:
            case RTM_DELLINK:
                handle_link_msg(nlh);
                break;
            case RTM_NEWADDR:
            case RTM_DELADDR:
                handle_addr_msg(nlh);
                break;
            case NLMSG_DONE:
            case NLMSG_ERROR:
            default:
                break;
        }
    }
}
