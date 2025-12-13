#include "alert.h"
#include "parser.h"
#include "logger.h"
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>

static unsigned long prev_rx[128];
static unsigned long prev_tx[128];
static time_t prev_time = 0;

void alert_check_cycle(void) {
    // simple check: if rx_errors > threshold, log warn
    // also compute per-second rate
    time_t now = time(NULL);
    double elapsed = (prev_time == 0) ? 0 : difftime(now, prev_time);

    // iterate known interfaces via naive approach (parser has internal table)
    // We'll call list and then use get_iface_by_name to fetch to compute.
    // But parser doesn't expose array publicly; for brevity, re-open /sys/class/net
    // and compute diffs using get_iface_by_name
    FILE *f;
    // We will just go through ifnames by reading /sys/class/net again
    // For simplicity limit to 128 entries
    char ifname[64];
    // iterate
    DIR *d = opendir("/sys/class/net");
    if (!d) return;
    struct dirent *de;
    int idx = 0;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        iface_info_t *inf = get_iface_by_name(de->d_name);
        if (!inf) continue;
        // check errors
        if (inf->rx_err > 10 || inf->tx_err > 10) {
            log_warn("interface %s has rx_err=%lu tx_err=%lu", inf->ifname, inf->rx_err, inf->tx_err);
        }
        if (elapsed > 0) {
            unsigned long rx = inf->rx_bytes;
            unsigned long tx = inf->tx_bytes;
            unsigned long rx_diff = rx - prev_rx[idx];
            unsigned long tx_diff = tx - prev_tx[idx];
            double rx_rate = rx_diff / elapsed;
            double tx_rate = tx_diff / elapsed;
            // If rx_rate too high (demo threshold), warn
            if (rx_rate > 10000000.0) { // 10MB/s arbitrary
                log_warn("high traffic on %s: rx_rate=%.0f B/s", inf->ifname, rx_rate);
            }
            prev_rx[idx] = rx;
            prev_tx[idx] = tx;
        } else {
            prev_rx[idx] = inf->rx_bytes;
            prev_tx[idx] = inf->tx_bytes;
        }
        idx++;
        if (idx >= 128) break;
    }
    closedir(d);
    prev_time = now;
}
