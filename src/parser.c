#define _GNU_SOURCE
#include "parser.h"
#include "logger.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ifaddrs.h>
#include <arpa/inet.h>

#define MAX_IFACES 128

iface_info_t *iface_list = NULL;
static int iface_count = 0;

/* 链表辅助函数 */
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
    iface_info_t *current = iface_list;
    while (current) {
        iface_info_t *next = current->next;
        free(current);
        current = next;
    }
    iface_list = NULL;
    iface_count = 0;
}

static iface_info_t *find_iface_by_index(int ifindex) {
    for (iface_info_t *p = iface_list; p; p = p->next) {
        if (p->ifindex == ifindex) return p;
    }
    return NULL;
}

static iface_info_t *find_iface_by_name(const char *ifname) {
    for (iface_info_t *p = iface_list; p; p = p->next) {
        if (strcmp(p->ifname, ifname) == 0) return p;
    }
    return NULL;
}

/* 主功能函数 */
void init_iface_table(void) {
    // 清理现有链表
    free_iface_list();
    
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        log_err("getifaddrs failed");
        return;
    }
    
    // 第一次遍历：收集所有接口
    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_name) continue;
        
        // 检查是否已存在
        if (find_iface_by_name(ifa->ifa_name)) continue;
        
        // 创建新节点
        iface_info_t *new_iface = create_iface_node();
        if (!new_iface) {
            log_err("Failed to create iface node for %s", ifa->ifa_name);
            continue;
        }
        
        // 初始化接口基本信息
        strncpy(new_iface->ifname, ifa->ifa_name, IFNAMSIZ - 1);
        new_iface->ifname[IFNAMSIZ - 1] = '\0';
        new_iface->ifindex = if_nametoindex(ifa->ifa_name);
        new_iface->up = (ifa->ifa_flags & IFF_UP) ? 1 : 0;
        new_iface->rx_bytes = 0;
        new_iface->tx_bytes = 0;
        new_iface->rx_err = 0;
        new_iface->tx_err = 0;
        new_iface->addr_cnt = 0;
        
        // 添加到链表头部
        new_iface->next = iface_list;
        iface_list = new_iface;
        iface_count++;
    }
    
    // 第二次遍历：收集IP地址
    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_name || !ifa->ifa_addr) continue;
        
        iface_info_t *iface = find_iface_by_name(ifa->ifa_name);
        if (!iface) continue;
        
        // 获取IP地址
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
    iface_info_t *inf = get_iface_by_index(ifindex);
    if (inf) return inf;
    
    // 创建新接口节点
    iface_info_t *new_iface = create_iface_node();
    if (!new_iface) {
        log_err("Failed to create iface node for index %d", ifindex);
        return NULL;
    }
    
    new_iface->ifindex = ifindex;
    if (ifname && ifname[0] != '\0') {
        strncpy(new_iface->ifname, ifname, IFNAMSIZ - 1);
        new_iface->ifname[IFNAMSIZ - 1] = '\0';
    } else {
        snprintf(new_iface->ifname, IFNAMSIZ, "if%d", ifindex);
    }
    
    new_iface->up = 0;  // 默认状态
    new_iface->rx_bytes = 0;
    new_iface->tx_bytes = 0;
    new_iface->rx_err = 0;
    new_iface->tx_err = 0;
    new_iface->addr_cnt = 0;
    
    // 添加到链表头部
    new_iface->next = iface_list;
    iface_list = new_iface;
    iface_count++;
    
    log_info("register iface: %s idx=%d", new_iface->ifname, new_iface->ifindex);
    return new_iface;
}

iface_info_t *get_iface_by_index(int ifindex) {
    return find_iface_by_index(ifindex);
}

iface_info_t *get_iface_by_name(const char *ifname) {
    return find_iface_by_name(ifname);
}

void update_iface_status(int ifindex, int up) {
    iface_info_t *inf = get_iface_by_index(ifindex);
    if (!inf) return;
    inf->up = up;
    log_info("iface %s (idx %d) status -> %s", inf->ifname, ifindex, up ? "UP" : "DOWN");
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

/* 更新IP（旧函数，保持兼容性）*/
void update_iface_ip(int ifindex, const char *ip) {
    iface_info_t *inf = get_iface_by_index(ifindex);
    if (!inf) return;
    
    // 这里只更新第一个IPv4地址作为主IP（兼容旧代码）
    for (int i = 0; i < inf->addr_cnt; i++) {
        if (inf->addrs[i].family == AF_INET) {
            strncpy(inf->addrs[i].addr, ip, INET6_ADDRSTRLEN - 1);
            inf->addrs[i].addr[INET6_ADDRSTRLEN - 1] = '\0';
            log_info("updated IP for iface %s (idx %d) -> %s", 
                    inf->ifname, ifindex, ip);
            return;
        }
    }
    
    // 如果没有IPv4地址，添加一个
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
    
    // 检查参数有效性
    if (family != AF_INET && family != AF_INET6) {
        log_warn("Invalid address family: %d", family);
        return;
    }

    // 前缀长度检查
    if(prefixlen == 0) {
        log_info("Prefix length is zero, skipping address addition");
        return;
    }
    
    // 去重检查
    for (int i = 0; i < inf->addr_cnt; i++) {
        if (inf->addrs[i].family == family &&
            inf->addrs[i].prefixlen == prefixlen &&
            strcmp(inf->addrs[i].addr, addr) == 0) {
            return;  // 地址已存在
        }
    }
    
    if (inf->addr_cnt >= MAX_ADDR_PER_IF) {
        log_warn("iface %s addr list full (max %d)", inf->ifname, MAX_ADDR_PER_IF);
        return;
    }
    
    // 添加新地址
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
            
            // 移除地址
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

void list_interfaces(void) {
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
}

/* 新增功能函数 */
void cleanup_iface_table(void) {
    free_iface_list();
    log_info("iface table cleaned up");
}

int get_iface_count(void) {
    return iface_count;
}

iface_info_t *get_iface_list(void) {
    return iface_list;
}

/* 删除接口 */
void delete_iface_by_index(int ifindex) {
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
            return;
        }
        prev = current;
        current = current->next;
    }
    
    log_info("iface with index %d not found for deletion", ifindex);
}

/* 遍历接口的回调函数接口 */
void foreach_iface(void (*callback)(iface_info_t *iface, void *data), void *data) {
    for (iface_info_t *p = iface_list; p; p = p->next) {
        callback(p, data);
    }
}
