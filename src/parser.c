#define _GNU_SOURCE
#include "parser.h"
#include "logger.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/if_link.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#define MAX_IFACES 128

iface_info_t *iface_list = NULL;
static int iface_count = 0;

// Thread-safe lock for iface_list
pthread_rwlock_t iface_list_lock = PTHREAD_RWLOCK_INITIALIZER;

// list management helper functions
static iface_info_t *create_iface_node(void) {
    iface_info_t *node = (iface_info_t *)calloc(1, sizeof(iface_info_t));
    if (!node) {
        log_err("Failed to allocate iface node");
        return NULL;
    }
    node->next = NULL;
    node->addr_cnt = 0;
    return node;
}

static void free_iface_list(void) {
    iface_list_wrlock();
    iface_info_t *current = iface_list;
    while (current) {
        iface_info_t *next = current->next;
        free(current);
        current = next;
    }
    iface_list = NULL;
    iface_count = 0;
    iface_list_unlock();
}

static iface_info_t *find_iface_by_index(int ifindex) {
    for (iface_info_t *p = iface_list; p; p = p->next) {
        if (p->ifindex == ifindex) return p;
    }
    return NULL;
}

static iface_info_t *find_iface_by_name(const char *ifname) {
    if (!ifname) return NULL;
    for (iface_info_t *p = iface_list; p; p = p->next) {
        if (strcmp(p->ifname, ifname) == 0) return p;
    }
    return NULL;
}

static void set_iface_name(iface_info_t *iface, const char *ifname) {
    if (!iface || !ifname || ifname[0] == '\0') return;
    snprintf(iface->ifname, IFNAMSIZ, "%s", ifname);
}

static iface_info_t *ensure_iface_by_index_locked(int ifindex, const char *ifname) {
    iface_info_t *inf = find_iface_by_index(ifindex);
    if (inf) {
        set_iface_name(inf, ifname);
        return inf;
    }

    iface_info_t *new_iface = create_iface_node();
    if (!new_iface) {
        log_err("Failed to create iface node for index %d", ifindex);
        return NULL;
    }

    new_iface->ifindex = ifindex;
    if (ifname && ifname[0] != '\0') {
        set_iface_name(new_iface, ifname);
    } else {
        snprintf(new_iface->ifname, IFNAMSIZ, "if%d", ifindex);
    }
    new_iface->up = 0;
    new_iface->next = iface_list;
    iface_list = new_iface;
    iface_count++;

    log_info("register iface: %s idx=%d", new_iface->ifname, new_iface->ifindex);
    return new_iface;
}

// main functions
void init_iface_table(void) {
    // cleanup existing list
    free_iface_list();
    
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        log_err("getifaddrs failed");
        return;
    }
    
    // the first pass: create iface nodes
    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_name) continue;
        
        // check if already exists
        if (find_iface_by_name(ifa->ifa_name)) continue;
        
        // create new node
        iface_info_t *new_iface = create_iface_node();
        if (!new_iface) {
            log_err("Failed to create iface node for %s", ifa->ifa_name);
            continue;
        }
        
        // initialize iface basic info
        strncpy(new_iface->ifname, ifa->ifa_name, IFNAMSIZ - 1);
        new_iface->ifname[IFNAMSIZ - 1] = '\0';
        new_iface->ifindex = if_nametoindex(ifa->ifa_name);
        new_iface->up = (ifa->ifa_flags & IFF_UP) ? 1 : 0;
        new_iface->rx_bytes = 0;
        new_iface->tx_bytes = 0;
        new_iface->rx_err = 0;
        new_iface->tx_err = 0;
        new_iface->addr_cnt = 0;
        
        // add to the head of the list
        iface_list_wrlock();
        new_iface->next = iface_list;
        iface_list = new_iface;
        iface_count++;
        iface_list_unlock();
    }
    
    // the second pass: collect IP addresses
    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_name || !ifa->ifa_addr) continue;
        
        iface_info_t *iface = find_iface_by_name(ifa->ifa_name);
        if (!iface) continue;
        
        // get IP address
        char addr_str[INET6_ADDRSTRLEN] = {0};
        int family = ifa->ifa_addr->sa_family;
        int prefix_len = 0;
        
        if (family == AF_INET) {
            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &(sa->sin_addr), addr_str, INET_ADDRSTRLEN);
        } else if (family == AF_INET6) {
            struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)ifa->ifa_addr;
            inet_ntop(AF_INET6, &(sa6->sin6_addr), addr_str, INET6_ADDRSTRLEN);
        } else {
            continue;
        }
        
        if (strlen(addr_str) > 0) {
            iface_add_addr(iface, family, addr_str, prefix_len);
        }
    }
    
    freeifaddrs(ifaddr);
    log_info("init_iface_table found %d interfaces", iface_count);
}

iface_info_t *ensure_iface_by_index(int ifindex, const char *ifname) {
    iface_list_wrlock();
    iface_info_t *inf = ensure_iface_by_index_locked(ifindex, ifname);
    iface_list_unlock();
    return inf;
}

iface_info_t *get_iface_by_index(int ifindex) {
    return find_iface_by_index(ifindex);
}

iface_info_t *get_iface_by_name(const char *ifname) {
    return find_iface_by_name(ifname);
}

void update_iface_status(int ifindex, int up) {
    upsert_iface_link(ifindex, NULL, up);
}

void upsert_iface_link(int ifindex, const char *ifname, int up) {
    iface_list_wrlock();
    iface_info_t *inf = ensure_iface_by_index_locked(ifindex, ifname);
    if (!inf) {
        iface_list_unlock();
        return;
    }
    inf->up = up;
    log_info("iface %s (idx %d) status -> %s", inf->ifname, ifindex, up ? "UP" : "DOWN");
    iface_list_unlock();
}

void update_iface_counters(int ifindex, unsigned long rx_bytes, unsigned long tx_bytes, 
                          unsigned long rx_err, unsigned long tx_err) {
    iface_info_t *inf = get_iface_by_index(ifindex);
    if (!inf) return;
    inf->rx_bytes = rx_bytes;
    inf->tx_bytes = tx_bytes;
    inf->rx_err = rx_err;
    inf->tx_err = tx_err;
}

/* update IP (old function, for compatibility) */
void update_iface_ip(int ifindex, const char *ip) {
    iface_info_t *inf = get_iface_by_index(ifindex);
    if (!inf) return;
    
    // only update the first IPv4 address as the primary IP (for compatibility with old code)
    for (int i = 0; i < inf->addr_cnt; i++) {
        if (inf->addrs[i].family == AF_INET) {
            strncpy(inf->addrs[i].addr, ip, INET6_ADDRSTRLEN - 1);
            inf->addrs[i].addr[INET6_ADDRSTRLEN - 1] = '\0';
            log_info("updated IP for iface %s (idx %d) -> %s", 
                    inf->ifname, ifindex, ip);
            return;
        }
    }
    
    // if no IPv4 address, add one
    if (inf->addr_cnt < MAX_ADDR_PER_IF) {
        inf->addrs[inf->addr_cnt].family = AF_INET;
        strncpy(inf->addrs[inf->addr_cnt].addr, ip, INET6_ADDRSTRLEN - 1);
        inf->addrs[inf->addr_cnt].addr[INET6_ADDRSTRLEN - 1] = '\0';
        inf->addr_cnt++;
        log_info("added IP for iface %s (idx %d) -> %s", 
                inf->ifname, ifindex, ip);
    }
}

void iface_add_addr(iface_info_t *inf, int family, const char *addr, int prefixlen) {
    if (!inf || !addr || !addr[0]) return;
    
    // check parameter validity
    if (family != AF_INET && family != AF_INET6) {
        log_warn("Invalid address family: %d", family);
        return;
    }

    // check prefix length
    if(prefixlen == 0) {
        log_info("Prefix length is zero, skipping address addition");
        return;
    }
    
    // deduplication check
    for (int i = 0; i < inf->addr_cnt; i++) {
        if (inf->addrs[i].family == family &&
            inf->addrs[i].prefixlen == prefixlen &&
            strcmp(inf->addrs[i].addr, addr) == 0) {
            return;  // address already exists
        }
    }
    
    if (inf->addr_cnt >= MAX_ADDR_PER_IF) {
        log_warn("iface %s addr list full (max %d)", inf->ifname, MAX_ADDR_PER_IF);
        return;
    }
    
    // add new address
    inf->addrs[inf->addr_cnt].family = family;
    inf->addrs[inf->addr_cnt].prefixlen = prefixlen;
    strncpy(inf->addrs[inf->addr_cnt].addr, addr, INET6_ADDRSTRLEN - 1);
    inf->addrs[inf->addr_cnt].addr[INET6_ADDRSTRLEN - 1] = '\0';
    inf->addr_cnt++;
    
    log_info("iface %s add addr %s (family: %s)", inf->ifname, addr,
            family == AF_INET ? "IPv4" : "IPv6");
}

void iface_del_addr(iface_info_t *inf, int family, const char *addr, int prefixlen) {
    if (!inf || !addr || !addr[0]) return;
    
    for (int i = 0; i < inf->addr_cnt; i++) {
        if (inf->addrs[i].family == family &&
            inf->addrs[i].prefixlen == prefixlen &&
            strcmp(inf->addrs[i].addr, addr) == 0) {
            
            // remove address
            for (int j = i; j < inf->addr_cnt - 1; j++) {
                inf->addrs[j] = inf->addrs[j + 1];
            }
            inf->addr_cnt--;
            
            log_info("iface %s del addr %s (family: %s)", inf->ifname, addr,
                    family == AF_INET ? "IPv4" : "IPv6");
            return;
        }
    }
    
    log_info("iface %s addr %s not found for deletion", inf->ifname, addr);
}

void iface_add_addr_by_index(int ifindex, const char *ifname, int family, const char *addr, int prefixlen) {
    iface_list_wrlock();
    iface_info_t *inf = ensure_iface_by_index_locked(ifindex, ifname);
    if (inf) {
        iface_add_addr(inf, family, addr, prefixlen);
    }
    iface_list_unlock();
}

void iface_del_addr_by_index(int ifindex, int family, const char *addr, int prefixlen) {
    iface_list_wrlock();
    iface_info_t *inf = find_iface_by_index(ifindex);
    if (inf) {
        iface_del_addr(inf, family, addr, prefixlen);
    }
    iface_list_unlock();
}

void list_interfaces(void) {
    iface_list_rdlock();
    printf("=== Network Interfaces (%d) ===\n", iface_count);
    for (iface_info_t *p = iface_list; p; p = p->next) {
        printf("Interface: %s\n", p->ifname);
        printf("  Index: %d, Status: %s\n", p->ifindex, p->up ? "UP" : "DOWN");
        printf("  Counters: RX=%lu TX=%lu RX_ERR=%lu TX_ERR=%lu\n",
               p->rx_bytes, p->tx_bytes, p->rx_err, p->tx_err);
        
        if (p->addr_cnt > 0) {
            printf("  Addresses (%d):\n", p->addr_cnt);
            for (int i = 0; i < p->addr_cnt; i++) {
                printf("    [%d] %s (%s)\n", i + 1, p->addrs[i].addr,
                       p->addrs[i].family == AF_INET ? "IPv4" : "IPv6");
            }
        } else {
            printf("  No addresses\n");
        }
        printf("\n");
    }
    iface_list_unlock();
}

// new helper functions
void cleanup_iface_table(void) {
    free_iface_list();
    log_info("iface table cleaned up");
}

int get_iface_count(void) {
    int count;
    iface_list_rdlock();
    count = iface_count;
    iface_list_unlock();
    return count;
}

iface_info_t *get_iface_list(void) {
    // Deprecated: Use get_iface_list_safe() instead for thread safety
    return iface_list;
}

// delete iface by index
void delete_iface_by_index(int ifindex) {
    iface_list_wrlock();
    
    iface_info_t *prev = NULL;
    iface_info_t *current = iface_list;
    
    while (current) {
        if (current->ifindex == ifindex) {
            if (prev) {
                prev->next = current->next;
            } else {
                iface_list = current->next;
            }
            
            log_info("deleted iface: %s idx=%d", current->ifname, current->ifindex);
            free(current);
            iface_count--;
            iface_list_unlock();
            return;
        }
        prev = current;
        current = current->next;
    }
    
    iface_list_unlock();
    log_info("iface with index %d not found for deletion", ifindex);
}

// iterate over interfaces with a callback
void foreach_iface(void (*callback)(iface_info_t *iface, void *data), void *data) {
    iface_list_rdlock();
    for (iface_info_t *p = iface_list; p; p = p->next) {
        callback(p, data);
    }
    iface_list_unlock();
}

// Helper function to read unsigned long from sysfs file
static unsigned long read_ull_file(const char *path) {
    unsigned long v = 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    if (fscanf(f, "%lu", &v) != 1) v = 0;
    fclose(f);
    return v;
}

// Update performance data for a single interface
int update_iface_performance_data(iface_info_t *iface) {
    if (!iface || !iface->ifname[0]) return -1;
    
    // First try Netlink method (preferred)
    if (update_iface_stats_via_netlink(iface) == 0) {
        return 0;
    }
    
    // Fallback to sysfs method if Netlink fails
    log_warn("Netlink stats update failed for %s, falling back to sysfs", iface->ifname);
    
    char rx_path[256], tx_path[256], rxerr_path[256], txerr_path[256];
    snprintf(rx_path, sizeof(rx_path), "/sys/class/net/%s/statistics/rx_bytes", iface->ifname);
    snprintf(tx_path, sizeof(tx_path), "/sys/class/net/%s/statistics/tx_bytes", iface->ifname);
    snprintf(rxerr_path, sizeof(rxerr_path), "/sys/class/net/%s/statistics/rx_errors", iface->ifname);
    snprintf(txerr_path, sizeof(txerr_path), "/sys/class/net/%s/statistics/tx_errors", iface->ifname);
    
    iface->rx_bytes = read_ull_file(rx_path);
    iface->tx_bytes = read_ull_file(tx_path);
    iface->rx_err = read_ull_file(rxerr_path);
    iface->tx_err = read_ull_file(txerr_path);
    
    // Also update the new stats structure for consistency
    iface->stats.rx_bytes = iface->rx_bytes;
    iface->stats.tx_bytes = iface->tx_bytes;
    iface->stats.rx_errors = iface->rx_err;
    iface->stats.tx_errors = iface->tx_err;
    
    return 0;
}

// Update performance data for all interfaces
void update_all_iface_performance_data(void) {
    for (iface_info_t *p = iface_list; p; p = p->next) {
        update_iface_performance_data(p);
    }
}

// Helper function to synchronize legacy statistics fields
void sync_legacy_stats_fields(iface_info_t *iface) {
    if (!iface) return;
    
    // Sync the legacy fields with the new stats structure
    iface->rx_bytes = iface->stats.rx_bytes;
    iface->tx_bytes = iface->stats.tx_bytes;
    iface->rx_err = iface->stats.rx_errors;
    iface->tx_err = iface->stats.tx_errors;
}

// Send Netlink request to get interface statistics
static int send_netlink_stats_request(int sock, int ifindex) {
    struct {
        struct nlmsghdr nlh;
        struct ifinfomsg ifm;
        struct rtattr rta;
        __u32 filter_mask;
    } req;

    memset(&req, 0, sizeof(req));

    // Netlink message header
    req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg) + sizeof(struct rtattr) + sizeof(__u32));
    req.nlh.nlmsg_type = RTM_GETLINK;
    req.nlh.nlmsg_flags = NLM_F_REQUEST;
    req.nlh.nlmsg_seq = time(NULL);
    req.nlh.nlmsg_pid = getpid();

    // Interface information message
    req.ifm.ifi_family = AF_UNSPEC;
    req.ifm.ifi_index = ifindex;

    // Statistics filter attribute
    req.rta.rta_type = IFLA_EXT_MASK;
    req.rta.rta_len = RTA_LENGTH(sizeof(__u32));
    req.filter_mask = RTEXT_FILTER_VF;

    struct sockaddr_nl nladdr = {
        .nl_family = AF_NETLINK,
        .nl_pid = 0,   // kernel
    };

    struct iovec iov = {
        .iov_base = &req,
        .iov_len = req.nlh.nlmsg_len,
    };

    struct msghdr msg = {
        .msg_name = &nladdr,
        .msg_namelen = sizeof(nladdr),
        .msg_iov = &iov,
        .msg_iovlen = 1,
    };

    int ret = sendmsg(sock, &msg, 0);
    if (ret < 0) {
        log_err("send RTM_GETLINK failed: %s", strerror(errno));
    }

    return ret;
}

// Parse Netlink response and update interface statistics
static int parse_netlink_stats_response(struct nlmsghdr *nlh, iface_info_t *iface) {
    struct ifinfomsg *ifm = NLMSG_DATA(nlh);
    
    // Only process messages for our target interface
    if (ifm->ifi_index != iface->ifindex) {
        return 0;
    }

    // Parse attributes to find statistics
    struct rtattr *tb[IFLA_MAX + 1];
    memset(tb, 0, sizeof(tb));
    struct rtattr *rta = IFLA_RTA(ifm);
    int len = IFLA_PAYLOAD(nlh);
    
    while (RTA_OK(rta, len)) {
        if (rta->rta_type <= IFLA_MAX) {
            tb[rta->rta_type] = rta;
        }
        rta = RTA_NEXT(rta, len);
    }

    // Extract statistics if available
    if (tb[IFLA_STATS64]) {
        const void *stats = RTA_DATA(tb[IFLA_STATS64]);
        memcpy(&iface->stats, stats, sizeof(iface->stats));
        return 1;
    }

    return 0;
}

// Netlink-based performance data update for a single interface
int update_iface_stats_via_netlink(iface_info_t *iface) {
    if (!iface || !iface->ifname[0]) return -1;

    int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (sock < 0) {
        log_err("Netlink socket creation failed: %s", strerror(errno));
        return -1;
    }

    // Send request for this interface
    if (send_netlink_stats_request(sock, iface->ifindex) < 0) {
        close(sock);
        return -1;
    }

    // Read and parse response
    char buf[4096];
    struct iovec iov = { .iov_base = buf, .iov_len = sizeof(buf) };
    struct msghdr msg = {
        .msg_name = NULL,
        .msg_namelen = 0,
        .msg_iov = &iov,
        .msg_iovlen = 1,
    };

    int success = 0;
    ssize_t n = recvmsg(sock, &msg, 0);
    if (n > 0) {
        struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
        for (; NLMSG_OK(nlh, n); nlh = NLMSG_NEXT(nlh, n)) {
            if (nlh->nlmsg_type == RTM_NEWLINK) {
                if (parse_netlink_stats_response(nlh, iface)) {
                    success = 1;
                    break;
                }
            }
        }
    }

    close(sock);
    return success ? 0 : -1;
}

// Netlink-based performance data update for all interfaces
void update_all_iface_stats_via_netlink(void) {
    // For simplicity, iterate through each interface
    // In a more optimized implementation, we could batch request all interfaces
    iface_list_rdlock();
    for (iface_info_t *p = iface_list; p; p = p->next) {
        update_iface_stats_via_netlink(p);
    }
    iface_list_unlock();
}

// Thread-safe access functions implementation

// Get a safe copy of the iface list (caller must free after use)
int get_iface_snapshot_by_name(const char *ifname, iface_info_t *out) {
    if (!ifname || !out) return -1;

    int found = -1;
    iface_list_rdlock();
    iface_info_t *src = find_iface_by_name(ifname);
    if (src) {
        memcpy(out, src, sizeof(iface_info_t));
        out->next = NULL;
        found = 0;
    }
    iface_list_unlock();
    return found;
}

iface_info_t *get_iface_list_safe(void) {
    iface_list_rdlock();
    
    // Create a deep copy of the list
    iface_info_t *head = NULL;
    iface_info_t **tail = &head;
    
    for (iface_info_t *src = iface_list; src; src = src->next) {
        iface_info_t *dest = (iface_info_t *)calloc(1, sizeof(iface_info_t));
        if (!dest) {
            // Free any partially copied list
            while (head) {
                iface_info_t *next = head->next;
                free(head);
                head = next;
            }
            iface_list_unlock();
            return NULL;
        }
        
        // Copy all fields
        memcpy(dest, src, sizeof(iface_info_t));
        dest->next = NULL;
        
        *tail = dest;
        tail = &dest->next;
    }
    
    iface_list_unlock();
    return head;
}

// Lock management functions
void iface_list_rdlock(void) {
    int ret = pthread_rwlock_rdlock(&iface_list_lock);
    if (ret != 0) {
        log_err("Failed to acquire read lock: %s", strerror(ret));
    }
}

void iface_list_wrlock(void) {
    int ret = pthread_rwlock_wrlock(&iface_list_lock);
    if (ret != 0) {
        log_err("Failed to acquire write lock: %s", strerror(ret));
    }
}

void iface_list_unlock(void) {
    int ret = pthread_rwlock_unlock(&iface_list_lock);
    if (ret != 0) {
        log_err("Failed to release lock: %s", strerror(ret));
    }
}
