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
#include <time.h>

/* netlink socket */
static int nl_sock = -1;
int netlink_fd(void) { return nl_sock; }

static int make_socket_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        log_err("fcntl F_GETFL failed for fd %d: %s", fd, strerror(errno));
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        log_err("fcntl F_SETFL O_NONBLOCK failed for fd %d: %s", fd, strerror(errno));
        return -1;
    }
    return 0;
}

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

static const void *addr_to_str(int family, void *addr, char *buf, size_t len) {
    if (family == AF_INET)  inet_ntop(AF_INET, addr, buf, len);
    if (family == AF_INET6) inet_ntop(AF_INET6, addr, buf, len);
    return NULL;
}

static int send_nl_addr_dump_req(int sock)
{
    struct {
        struct nlmsghdr nlh;
        struct ifaddrmsg ifa;
    } req;

    memset(&req, 0, sizeof(req));

    req.nlh.nlmsg_len   = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
    req.nlh.nlmsg_type  = RTM_GETADDR;
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.nlh.nlmsg_seq   = time(NULL);
    req.nlh.nlmsg_pid   = getpid();

    req.ifa.ifa_family = AF_UNSPEC;  /* IPv4 + IPv6 */

    struct sockaddr_nl nladdr = {
        .nl_family = AF_NETLINK,
        .nl_pid    = 0,   /* kernel */
    };

    struct iovec iov = {
        .iov_base = &req,
        .iov_len  = req.nlh.nlmsg_len,
    };

    struct msghdr msg = {
        .msg_name    = &nladdr,
        .msg_namelen = sizeof(nladdr),
        .msg_iov     = &iov,
        .msg_iovlen  = 1,
    };

    int ret = sendmsg(sock, &msg, 0);
    if (ret < 0) {
        log_err("send RTM_GETADDR failed: %s", strerror(errno));
    }

    return ret;
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
        const char *ifname = RTA_DATA(tb[IFLA_IFNAME]);
        if (!get_iface_by_name(ifname)) {
            log_info("link event for unknown ifname=%s ifindex=%d up=%d", ifname, ifindex, is_up);
        }
    }

    update_iface_status(ifindex, is_up);
}

/* handle address (RTM_NEWADDR / RTM_DELADDR) */
static void handle_addr_msg(struct nlmsghdr *nlh) {
    struct ifaddrmsg *ifa = NLMSG_DATA(nlh);
    int ifindex = ifa->ifa_index;
    int family = ifa->ifa_family; /* AF_INET or AF_INET6 */
    int prefixlen = ifa->ifa_prefixlen;
    
    if (ifa->ifa_prefixlen == 0)  return;

    struct rtattr *tb[IFA_MAX + 1];
    memset(tb, 0, sizeof(tb));
    struct rtattr *rta = IFA_RTA(ifa);
    int len = IFA_PAYLOAD(nlh);
    rtattr_get(tb, IFA_MAX, rta, len);

    char addr_str[INET6_ADDRSTRLEN] = {0};
    
    void *addr = NULL;
    if (tb[IFA_LOCAL]) {
        addr = RTA_DATA(tb[IFA_LOCAL]);
    } else if (tb[IFA_ADDRESS]) {
        addr = RTA_DATA(tb[IFA_ADDRESS]);
    }
    if (addr) {
        addr_to_str(family, addr, addr_str, sizeof(addr_str));
    }

    if (nlh->nlmsg_type == RTM_NEWADDR) {
        log_info("NEWADDR on ifindex=%d family=%d addr=%s", ifindex, family, addr_str[0]?addr_str:"<none>");
        if (addr_str[0]) {
            iface_info_t *inf = get_iface_by_index(ifindex);
            if (!inf) {
                inf = ensure_iface_by_index(ifindex, NULL);
            }
            if (inf) {
                iface_add_addr(inf, family, addr_str, prefixlen);
            }
        }
    } else if (nlh->nlmsg_type == RTM_DELADDR) {
        log_info("DELADDR on ifindex=%d family=%d addr=%s", ifindex, family, addr_str[0]?addr_str:"<none>");
        /* On address delete we might clear IP if matches; simple approach: clear if equal */
        /* parser provides helper to clear if ip matches */
        iface_info_t *inf = get_iface_by_index(ifindex);
        if (!inf) {
            inf = ensure_iface_by_index(ifindex, NULL);
        }
        if (inf && addr_str[0]) {
            iface_del_addr(inf, family, addr_str, prefixlen);
        }
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
        nl_sock = -1;
        return -1;
    }

    if (make_socket_non_blocking(nl_sock) < 0) {
        close(nl_sock);
        nl_sock = -1;
        return -1;
    }

    /* add to epoll; EPOLLET requires process_netlink_messages() to drain the socket */
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = nl_sock;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, nl_sock, &ev) < 0) {
        log_err("epoll_ctl add nl_sock failed: %s", strerror(errno));
        close(nl_sock);
        nl_sock = -1;
        return -1;
    }
    log_info("netlink socket started (fd=%d)", nl_sock);
    log_info("syncing netlink state...");
    send_nl_addr_dump_req(nl_sock);
    return nl_sock;
}

/* main message processing */
void process_netlink_messages(void) {
    if (nl_sock < 0) return;

    for (;;) {
        char buf[8192];
        struct sockaddr_nl sa;
        struct iovec iov = { .iov_base = buf, .iov_len = sizeof(buf) };
        struct msghdr msg = {
            .msg_name = &sa,
            .msg_namelen = sizeof(sa),
            .msg_iov = &iov,
            .msg_iovlen = 1,
        };

        ssize_t len = recvmsg(nl_sock, &msg, 0);
        if (len < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == ENOBUFS) {
                log_warn("netlink receive buffer overrun: %s", strerror(errno));
                continue;
            }
            log_err("recvmsg nl_sock failed: %s", strerror(errno));
            break;
        }
        if (len == 0) {
            log_warn("netlink socket returned EOF");
            break;
        }
        if (msg.msg_flags & MSG_TRUNC) {
            log_warn("netlink message truncated; consider increasing receive buffer");
            continue;
        }

        ssize_t remaining = len;
        for (struct nlmsghdr *nlh = (struct nlmsghdr *)buf; NLMSG_OK(nlh, remaining); nlh = NLMSG_NEXT(nlh, remaining)) {
            if (nlh->nlmsg_type == NLMSG_ERROR) {
                struct nlmsgerr *err = NLMSG_DATA(nlh);
                if (nlh->nlmsg_len >= NLMSG_LENGTH(sizeof(*err)) && err->error != 0) {
                    log_warn("netlink reported error: %s", strerror(-err->error));
                }
                continue;
            }
            if (nlh->nlmsg_type == NLMSG_DONE) {
                log_info("netlink dump completed");
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
        if (remaining > 0) {
            log_warn("netlink message parse stopped with %zd trailing bytes", remaining);
        }
    }
}
