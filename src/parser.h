#ifndef PARSER_H
#define PARSER_H

#include <net/if.h>

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
    unsigned long rx_bytes;
    unsigned long tx_bytes;
    unsigned long rx_err;
    unsigned long tx_err;

    iface_addr_t addrs[MAX_ADDR_PER_IF];
    int addr_cnt;
    struct iface_info *next;
} iface_info_t;

/* 全局接口链表（只读） */
extern iface_info_t *iface_list;

void init_iface_table(void);
iface_info_t *get_iface_by_index(int ifindex);
iface_info_t *get_iface_by_name(const char *ifname);
void update_iface_status(int ifindex, int up);
void update_iface_counters(int ifindex, unsigned long rx_bytes, unsigned long tx_bytes, unsigned long rx_err, unsigned long tx_err);
void update_iface_ip(int ifindex, const char *ip); /* ip==NULL clears the stored ip */
void list_interfaces(void);
iface_info_t *ensure_iface_by_index(int ifindex, const char *ifname);

/* 地址操作 */
void iface_add_addr(iface_info_t *inf, int family, const char *addr, int prefixlen);
void iface_del_addr(iface_info_t *inf, int family, const char *addr, int prefixlen);

#endif
