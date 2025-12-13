#ifndef PARSER_H
#define PARSER_H

#include <net/if.h>

typedef struct iface_info {
    char ifname[IFNAMSIZ];
    int ifindex;
    int up;
    unsigned long rx_bytes;
    unsigned long tx_bytes;
    unsigned long rx_err;
    unsigned long tx_err;
} iface_info_t;

void init_iface_table(void);
iface_info_t *get_iface_by_index(int ifindex);
iface_info_t *get_iface_by_name(const char *ifname);
void update_iface_status(int ifindex, int up);
void update_iface_counters(int ifindex, unsigned long rx_bytes, unsigned long tx_bytes, unsigned long rx_err, unsigned long tx_err);
void list_interfaces(void);

#endif
