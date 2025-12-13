#define _GNU_SOURCE
#include "netlink.h"
#include "parser.h"
#include "logger.h"

#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/if_link.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <fcntl.h>

/* netlink socket */
static int nl_sock = -1;
int netlink_fd(void) { return nl_sock; }

/* helper: parse rtattr list */
static struct rtattr *rtattr_get(struct rtattr *tb[], int max, struct rtattr *rta, int len) {
    while (RTA_OK(rta, len)) {
        if (rta->rta_type <= max) {
            tb[rta->rta_type] = rta;
        }
        rta = RTA_NEXT(rta, len);
    }
    return NULL;
}

/* handle link (RTM_NEWLINK / RTM_DELLINK) */
static void handle_link_msg(struct nlmsghdr *nlh) {
    struct ifinfomsg *ifi = NLMSG_DATA(nlh);
    int ifindex = ifi->ifi_index;
    int is_up = (ifi->ifi_flags & IFF_RUNNING) ? 1 : 0;

    /* parse attributes to get ifname (IFLA_IFNAME) */
    struct rtattr *tb[IFLA_MAX + 1];
    memset(tb, 0, sizeof(tb));
    struct rtattr *rta = IFLA_RTA(ifi);
    int len = IFLA_PAYLOAD(nlh);
    rtattr_get(tb, IFLA_MAX, rta, len);

    if (tb[IFLA_IFNAME]) {
        const char *ifname = (const char *)RTA_DATA(tb[IFLA_IFNAME]);
        /* update parser table by name if we have it */
        iface_info_t *inf = get_iface_by_name(ifname);
        if (inf) {
            update_iface_status(ifindex, is_up);
        } else {
            /* maybe new iface, try to register (parser init uses getifaddrs only once) */
            log_info("link event for unknown ifname=%s ifindex=%d up=%d", ifname, ifindex, is_up);
            /* best-effort: call update by index (parser will ignore if not found) */
            update_iface_status(ifindex, is_up);
        }
    } else {
        update_iface_status(ifindex, is_up);
    }
}

/* handle address (RTM_NEWADDR / RTM_DELADDR) */
static void handle_addr_msg(struct nlmsghdr *nlh) {
    struct ifaddrmsg *ifa = NLMSG_DATA(nlh);
    int ifindex = ifa->ifa_index;
    int family = ifa->ifa_family; /* AF_INET or AF_INET6 */

    struct rtattr *tb[IFA_MAX + 1];
    memset(tb, 0, sizeof(tb));
    struct rtattr *rta = IFA_RTA(ifa);
    int len = IFA_PAYLOAD(nlh);
    rtattr_get(tb, IFA_MAX, rta, len);

    char addr_str[INET6_ADDRSTRLEN] = {0};

    if (tb[IFA_LOCAL]) {
        void *addr = RTA_DATA(tb[IFA_LOCAL]);
        if (family == AF_INET) {
            inet_ntop(AF_INET, addr, addr_str, sizeof(addr_str));
        } else if (family == AF_INET6) {
            inet_ntop(AF_INET6, addr, addr_str, sizeof(addr_str));
        }
    } else if (tb[IFA_ADDRESS]) {
        void *addr = RTA_DATA(tb[IFA_ADDRESS]);
        if (family == AF_INET) {
            inet_ntop(AF_INET, addr, addr_str, sizeof(addr_str));
        } else if (family == AF_INET6) {
            inet_ntop(AF_INET6, addr, addr_str, sizeof(addr_str));
        }
    }

    if (nlh->nlmsg_type == RTM_NEWADDR) {
        log_info("NEWADDR on ifindex=%d family=%d addr=%s", ifindex, family, addr_str[0]?addr_str:"<none>");
        if (addr_str[0]) {
            update_iface_ip(ifindex, addr_str);
        }
    } else if (nlh->nlmsg_type == RTM_DELADDR) {
        log_info("DELADDR on ifindex=%d family=%d addr=%s", ifindex, family, addr_str[0]?addr_str:"<none>");
        /* On address delete we might clear IP if matches; simple approach: clear if equal */
        /* parser provides helper to clear if ip matches */
        update_iface_ip(ifindex, NULL); /* clear stored IP for simplicity */
    }
}

/* handle route (RTM_NEWROUTE / RTM_DELROUTE) */
static void handle_route_msg(struct nlmsghdr *nlh) {
    struct rtmsg *rt = NLMSG_DATA(nlh);
    struct rtattr *tb[RTA_MAX + 1];
    memset(tb, 0, sizeof(tb));
    struct rtattr *rta = RTM_RTA(rt);
    int len = RTM_PAYLOAD(nlh);
    rtattr_get(tb, RTA_MAX, rta, len);

    char dst[INET6_ADDRSTRLEN] = {0};
    int oif = 0;

    if (tb[RTA_DST]) {
        void *addr = RTA_DATA(tb[RTA_DST]);
        if (rt->rtm_family == AF_INET) {
            inet_ntop(AF_INET, addr, dst, sizeof(dst));
        } else if (rt->rtm_family == AF_INET6) {
            inet_ntop(AF_INET6, addr, dst, sizeof(dst));
        }
    } else {
        /* default route */
        strcpy(dst, "0.0.0.0/0");
    }

    if (tb[RTA_OIF]) {
        oif = *(int *)RTA_DATA(tb[RTA_OIF]);
    }

    log_info("ROUTE event type=%d fam=%d dst=%s oif=%d", nlh->nlmsg_type, rt->rtm_family, dst, oif);
    /* Could update route-related structures here; for now just log */
}

/* start netlink socket and register to epoll */
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
    sa.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR | RTMGRP_IPV4_ROUTE | RTMGRP_IPV6_ROUTE;

    if (bind(nl_sock, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
        log_err("bind netlink failed: %s", strerror(errno));
        close(nl_sock);
        return -1;
    }

    /* set socket non-blocking (optional but good) */
    int flags = fcntl(nl_sock, F_GETFL, 0);
    if (flags >= 0) fcntl(nl_sock, F_SETFL, flags | O_NONBLOCK);

    /* add to epoll */
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

/* main message processing */
void process_netlink_messages(void) {
    char buf[8192];
    struct iovec iov = { buf, sizeof(buf) };
    struct sockaddr_nl sa;
    struct msghdr msg = { (void*)&sa, sizeof(sa), &iov, 1, NULL, 0, 0 };

    ssize_t len;
    while ((len = recvmsg(nl_sock, &msg, 0)) > 0) {
        for (struct nlmsghdr *nlh = (struct nlmsghdr*)buf; NLMSG_OK(nlh, (unsigned int)len); nlh = NLMSG_NEXT(nlh, len)) {
            if (nlh->nlmsg_type == NLMSG_ERROR || nlh->nlmsg_type == NLMSG_DONE) {
                continue;
            }
            switch (nlh->nlmsg_type) {
                case RTM_NEWLINK:
                case RTM_DELLINK:
                    handle_link_msg(nlh);
                    break;
                case RTM_NEWADDR:
                case RTM_DELADDR:
                    handle_addr_msg(nlh);
                    break;
                case RTM_NEWROUTE:
                case RTM_DELROUTE:
                    handle_route_msg(nlh);
                    break;
                default:
                    /* skip other types */
                    break;
            }
        }
    }
    if (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        log_err("recvmsg nl_sock failed: %s", strerror(errno));
    }
}
