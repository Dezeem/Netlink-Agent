#define _GNU_SOURCE
#include "metrics.h"
#include "parser.h"
#include "logger.h"
#include <stdio.h>
#include <dirent.h>
#include <string.h>

static unsigned long read_ull_file(const char *path) {
    unsigned long v = 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    if (fscanf(f, "%lu", &v) != 1) v = 0;
    fclose(f);
    return v;
}

void metrics_poll_once(void) {
    // iterate known interfaces and update counters from sysfs
    // for simplicity use getifaddrs-derived table in parser
    // we assume parser provides list_interfaces to iterate; here we directly use names.
    // A more robust impl would expose iteration API.
    // For brevity, call list_interfaces() to print; but actually update counters per entry:
    // We'll re-open /sys/class/net/<if>/statistics/* for each interface
    // Implement basic approach:
    struct {
        char name[64];
    } tmp;
    // Ugly but simple: read /sys/class/net directory
    DIR *d = opendir("/sys/class/net");
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char rx_path[256], tx_path[256], rxerr_path[256], txerr_path[256];
        snprintf(rx_path, sizeof(rx_path), "/sys/class/net/%s/statistics/rx_bytes", de->d_name);
        snprintf(tx_path, sizeof(tx_path), "/sys/class/net/%s/statistics/tx_bytes", de->d_name);
        snprintf(rxerr_path, sizeof(rxerr_path), "/sys/class/net/%s/statistics/rx_errors", de->d_name);
        snprintf(txerr_path, sizeof(txerr_path), "/sys/class/net/%s/statistics/tx_errors", de->d_name);
        unsigned long rx = read_ull_file(rx_path);
        unsigned long tx = read_ull_file(tx_path);
        unsigned long rxerr = read_ull_file(rxerr_path);
        unsigned long txerr = read_ull_file(txerr_path);
        iface_info_t *inf = get_iface_by_name(de->d_name);
        if (inf) {
            update_iface_counters(inf->ifindex, rx, tx, rxerr, txerr);
        }
    }
    closedir(d);
}
