#include "alert.h"
#include "parser.h"
#include "logger.h"
#include <time.h>
#include <string.h>
#include <stdlib.h>

#define MAX_INTERFACES 128

static unsigned long prev_rx[MAX_INTERFACES];
static unsigned long prev_tx[MAX_INTERFACES];
static time_t prev_time = 0;

// Structure to track interface indices for rate calculation
typedef struct {
    int index;
    char ifname[IFNAMSIZ];
} iface_tracker_t;

static iface_tracker_t iface_trackers[MAX_INTERFACES];
static int iface_count = 0;

// Callback function for alert checking
static void alert_check_callback(iface_info_t *iface, void *data) {
    time_t now = *(time_t *)data;
    double elapsed = (prev_time == 0) ? 0 : difftime(now, prev_time);
    
    // Find or create tracker for this interface
    int idx = -1;
    for (int i = 0; i < iface_count; i++) {
        if (strcmp(iface_trackers[i].ifname, iface->ifname) == 0) {
            idx = i;
            break;
        }
    }
    
    if (idx == -1) {
        if (iface_count < MAX_INTERFACES) {
            idx = iface_count;
            strncpy(iface_trackers[idx].ifname, iface->ifname, IFNAMSIZ - 1);
            iface_trackers[idx].ifname[IFNAMSIZ - 1] = '\0';
            iface_count++;
        } else {
            log_warn("Too many interfaces for alert tracking");
            return;
        }
    }
    
    // Check error thresholds
    if (iface->rx_err > 10 || iface->tx_err > 10) {
        log_warn("interface %s has rx_err=%lu tx_err=%lu", iface->ifname, iface->rx_err, iface->tx_err);
    }
    
    // Calculate traffic rates
    if (elapsed > 0) {
        unsigned long rx = iface->rx_bytes;
        unsigned long tx = iface->tx_bytes;
        unsigned long rx_diff = rx - prev_rx[idx];
        unsigned long tx_diff = tx - prev_tx[idx];
        double rx_rate = rx_diff / elapsed;
        (void)tx_diff; // Unused variable
        
        // Check for high traffic (10MB/s threshold)
        if (rx_rate > 10000000.0) {
            log_warn("high traffic on %s: rx_rate=%.0f B/s", iface->ifname, rx_rate);
        }
        
        prev_rx[idx] = rx;
        prev_tx[idx] = tx;
    } else {
        // First run, initialize previous values
        prev_rx[idx] = iface->rx_bytes;
        prev_tx[idx] = iface->tx_bytes;
    }
}

void alert_check_cycle(void) {
    time_t now = time(NULL);
    
    // Use SSOT architecture: iterate through interfaces using parser's unified interface
    foreach_iface(alert_check_callback, &now);
    
    prev_time = now;
}
