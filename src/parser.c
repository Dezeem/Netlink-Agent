#define _GNU_SOURCE
#include "parser.h"
#include "logger.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ifaddrs.h>
#include <arpa/inet.h>

#define MAX_IFACES 128

static iface_info_t ifaces[MAX_IFACES];
static int iface_count = 0;

void init_iface_table(void) {
    // basic populate from getifaddrs
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        log_err("getifaddrs failed");
        return;
    }
    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_name) continue;
        // avoid duplicates
        int found = 0;
        for (int i=0;i<iface_count;i++) {
            if (strcmp(ifaces[i].ifname, ifa->ifa_name)==0) { found = 1; break; }
        }
        if (found) continue;
        if (iface_count >= MAX_IFACES) break;
        strncpy(ifaces[iface_count].ifname, ifa->ifa_name, IFNAMSIZ-1);
        ifaces[iface_count].ifindex = if_nametoindex(ifa->ifa_name);
        ifaces[iface_count].up = (ifa->ifa_flags & IFF_UP) ? 1 : 0;
        ifaces[iface_count].rx_bytes = 0;
        ifaces[iface_count].tx_bytes = 0;
        ifaces[iface_count].rx_err = 0;
        ifaces[iface_count].tx_err = 0;
        iface_count++;
    }
    freeifaddrs(ifaddr);
    log_info("init_iface_table found %d interfaces", iface_count);
}

iface_info_t *get_iface_by_index(int ifindex) {
    for (int i=0;i<iface_count;i++) if (ifaces[i].ifindex == ifindex) return &ifaces[i];
    return NULL;
}
iface_info_t *get_iface_by_name(const char *ifname) {
    for (int i=0;i<iface_count;i++) if (strcmp(ifaces[i].ifname, ifname)==0) return &ifaces[i];
    return NULL;
}

void update_iface_status(int ifindex, int up) {
    iface_info_t *inf = get_iface_by_index(ifindex);
    if (!inf) return;
    inf->up = up;
    log_info("iface %s (idx %d) status -> %s", inf->ifname, ifindex, up ? "UP" : "DOWN");
}

void update_iface_counters(int ifindex, unsigned long rx_bytes, unsigned long tx_bytes, unsigned long rx_err, unsigned long tx_err) {
    iface_info_t *inf = get_iface_by_index(ifindex);
    if (!inf) return;
    inf->rx_bytes = rx_bytes;
    inf->tx_bytes = tx_bytes;
    inf->rx_err = rx_err;
    inf->tx_err = tx_err;
}

void list_interfaces(void) {
    printf("Interfaces:\n");
    for (int i=0;i<iface_count;i++) {
        printf("%s idx=%d up=%d rx=%lu tx=%lu rx_err=%lu tx_err=%lu\n",
            ifaces[i].ifname, ifaces[i].ifindex, ifaces[i].up,
            ifaces[i].rx_bytes, ifaces[i].tx_bytes, ifaces[i].rx_err, ifaces[i].tx_err);
    }
}
