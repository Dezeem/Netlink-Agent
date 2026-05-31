#ifndef PARSER_H
#define PARSER_H

#include <net/if.h>
#include <linux/if_link.h>  // For struct rtnl_link_stats64
#include <pthread.h>

#define MAX_ADDR_PER_IF 8
#define INET6_ADDRSTRLEN 46

typedef struct iface_addr {
    int family;
    int prefixlen;                     /* CIDR prefix */
    char addr[INET6_ADDRSTRLEN];
} iface_addr_t;

typedef struct iface_info {
    char ifname[IFNAMSIZ];
    int ifindex;
    int up;
    
    // Full Netlink statistics structure
    struct rtnl_link_stats64 stats;
    
    // Legacy fields for backward compatibility
    unsigned long rx_bytes;
    unsigned long tx_bytes;
    unsigned long rx_err;
    unsigned long tx_err;

    iface_addr_t addrs[MAX_ADDR_PER_IF];
    int addr_cnt;
    struct iface_info *next;
} iface_info_t;

// Thread-safe interface management
extern pthread_rwlock_t iface_list_lock;

// Thread-safe access functions
iface_info_t *get_iface_list_safe(void);
int get_iface_snapshot_by_name(const char *ifname, iface_info_t *out);
void iface_list_rdlock(void);
void iface_list_wrlock(void);
void iface_list_unlock(void);

// global iface list head (use with lock!)
extern iface_info_t *iface_list;

void init_iface_table(void);
iface_info_t *get_iface_by_index(int ifindex);
iface_info_t *get_iface_by_name(const char *ifname);
void update_iface_status(int ifindex, int up);
void upsert_iface_link(int ifindex, const char *ifname, int up);
void update_iface_counters(int ifindex, unsigned long rx_bytes, unsigned long tx_bytes, unsigned long rx_err, unsigned long tx_err);
void update_iface_ip(int ifindex, const char *ip); /* ip==NULL clears the stored ip */
void list_interfaces(void);
iface_info_t *ensure_iface_by_index(int ifindex, const char *ifname);
void delete_iface_by_index(int ifindex);

// addr management functions
void iface_add_addr(iface_info_t *inf, int family, const char *addr, int prefixlen);
void iface_del_addr(iface_info_t *inf, int family, const char *addr, int prefixlen);
void iface_add_addr_by_index(int ifindex, const char *ifname, int family, const char *addr, int prefixlen);
void iface_del_addr_by_index(int ifindex, int family, const char *addr, int prefixlen);

// SSOT iterator functions
void foreach_iface(void (*callback)(iface_info_t *iface, void *data), void *data);
int get_iface_count(void);

// Performance data management functions
int update_iface_performance_data(iface_info_t *iface);
void update_all_iface_performance_data(void);

// Netlink-based performance data functions
int update_iface_stats_via_netlink(iface_info_t *iface);
void update_all_iface_stats_via_netlink(void);

// Helper functions for statistics synchronization
void sync_legacy_stats_fields(iface_info_t *iface);

#endif
