#define _GNU_SOURCE
#include "cli.h"
#include "logger.h"
#include "parser.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <stdio.h>
#include <dirent.h>
#include <stdlib.h>

#define CLI_SOCKET_PATH "/tmp/nlagent.sock"
static int cli_sock = -1;
static int cli_epoll_fd = -1;

// Per-connection state
typedef struct cli_conn {
    int fd;
    char buf[256];
    int buf_len;
    struct cli_conn *next;
} cli_conn_t;

static cli_conn_t *cli_connections = NULL;

static void cli_conn_free(cli_conn_t *conn) {
    if (!conn) return;
    // Remove from linked list
    cli_conn_t **pp = &cli_connections;
    while (*pp) {
        if (*pp == conn) {
            *pp = conn->next;
            break;
        }
        pp = &(*pp)->next;
    }
    log_info("CLI session ended for fd %d", conn->fd);
    epoll_ctl(cli_epoll_fd, EPOLL_CTL_DEL, conn->fd, NULL);
    close(conn->fd);
    free(conn);
}

static cli_conn_t *cli_conn_find(int fd) {
    for (cli_conn_t *p = cli_connections; p; p = p->next) {
        if (p->fd == fd) return p;
    }
    return NULL;
}

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

static int cli_write(int fd, const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n > 0) {
            off += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            log_warn("cli fd %d send buffer full; response truncated", fd);
            return -1;
        }
        if (n < 0) {
            log_warn("write cli fd %d failed: %s", fd, strerror(errno));
            return -1;
        }
        return -1;
    }
    return 0;
}

int cli_start(int epoll_fd) {
    struct sockaddr_un addr;
    unlink(CLI_SOCKET_PATH);
    cli_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (cli_sock < 0) {
        log_err("cli socket create failed: %s", strerror(errno));
        return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CLI_SOCKET_PATH, sizeof(addr.sun_path)-1);
    if (bind(cli_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log_err("cli bind failed: %s", strerror(errno));
        close(cli_sock);
        cli_sock = -1;
        return -1;
    }
    if (listen(cli_sock, SOMAXCONN) < 0) {
        log_err("cli listen failed: %s", strerror(errno));
        close(cli_sock);
        cli_sock = -1;
        return -1;
    }
    if (make_socket_non_blocking(cli_sock) < 0) {
        close(cli_sock);
        cli_sock = -1;
        return -1;
    }
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = cli_sock;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, cli_sock, &ev) < 0) {
        log_err("epoll_ctl add cli_sock failed: %s", strerror(errno));
        close(cli_sock);
        cli_sock = -1;
        return -1;
    }
    cli_epoll_fd = epoll_fd;
    log_info("cli socket listening at %s", CLI_SOCKET_PATH);
    return cli_sock;
}

// Send welcome message and prompt
static void send_welcome_prompt(int conn) {
    const char *welcome = "=== Netlink Agent CLI ===\n"
                         "Available commands:\n"
                         "  show interfaces, list - Display interface status\n"
                         "  help - Show this help message\n"
                         "  quit, exit - Close connection\n"
                         "\n> ";
    cli_write(conn, welcome, strlen(welcome));
}

// Send command prompt
static void send_prompt(int conn) {
    const char *prompt = "> ";
    cli_write(conn, prompt, strlen(prompt));
}

// Handle individual command
static int handle_command(int conn, const char *command) {
    if (strncmp(command, "show interfaces", 15) == 0 || strncmp(command, "list", 4) == 0) {
        iface_info_t *inf = iface_list;
        char line[512];
        int total_interfaces = 0;

        // Count interfaces first
        while (inf) {
            total_interfaces++;
            inf = inf->next;
        }

        // Send header
        int len = snprintf(line, sizeof(line), "=== Network Interfaces (%d) ===\n", total_interfaces);
        cli_write(conn, line, (size_t)len);

        // Send interface details using thread-safe copy
        iface_info_t *safe_list = get_iface_list_safe();
        if (!safe_list) {
            cli_write(conn, "Error: Failed to get interface list\n", 35);
            return 0;
        }
        
        inf = safe_list;
        while (inf) {
            len = snprintf(line, sizeof(line),
                "Interface: %s\n"
                "  Index: %d, Status: %s\n"
                "  Counters: RX=%llu TX=%llu RX_ERR=%llu TX_ERR=%llu\n",
                inf->ifname,
                inf->ifindex,
                inf->up ? "UP" : "DOWN",
                (unsigned long long)inf->stats.rx_bytes,
                (unsigned long long)inf->stats.tx_bytes,
                (unsigned long long)inf->stats.rx_errors,
                (unsigned long long)inf->stats.tx_errors);
            cli_write(conn, line, (size_t)len);

            // Send IP addresses
            if (inf->addr_cnt > 0) {
                len = snprintf(line, sizeof(line), "  Addresses (%d):\n", inf->addr_cnt);
                cli_write(conn, line, (size_t)len);
                
                for (int i = 0; i < inf->addr_cnt; i++) {
                    len = snprintf(line, sizeof(line),
                        "    [%d] %s/%d (%s)\n",
                        i + 1,
                        inf->addrs[i].addr,
                        inf->addrs[i].prefixlen,
                        inf->addrs[i].family == AF_INET ? "IPv4" : "IPv6");
                    cli_write(conn, line, (size_t)len);
                }
            } else {
                cli_write(conn, "  No addresses\n", 15);
            }
            
            cli_write(conn, "\n", 1);
            inf = inf->next;
        }
        
        // Free the safe list copy
        inf = safe_list;
        while (inf) {
            iface_info_t *next = inf->next;
            free(inf);
            inf = next;
        }
        
        return 0; // Continue session
    }
    else if (strncmp(command, "help", 4) == 0) {
        const char *help = "Available commands:\n"
                         "  show interfaces, list - Display interface status\n"
                         "  help - Show this help message\n"
                         "  quit, exit - Close connection\n";
        cli_write(conn, help, strlen(help));
        return 0; // Continue session
    }
    else if (strncmp(command, "quit", 4) == 0 || strncmp(command, "exit", 4) == 0) {
        const char *goodbye = "Goodbye!\n";
        cli_write(conn, goodbye, strlen(goodbye));
        return 1; // End session
    }
    else {
        const char *resp = "Unknown command. Type 'help' for available commands.\n";
        cli_write(conn, resp, strlen(resp));
        return 0; // Continue session
    }
}

static int process_cli_buffer(cli_conn_t *conn) {
    char *cursor = conn->buf;
    char *newline;

    while ((newline = strchr(cursor, '\n')) != NULL) {
        *newline = '\0';

        char *cr = newline > cursor ? newline - 1 : NULL;
        if (cr && *cr == '\r') *cr = '\0';

        if (*cursor == '\0') {
            send_prompt(conn->fd);
            cursor = newline + 1;
            continue;
        }

        int should_exit = handle_command(conn->fd, cursor);
        if (should_exit) {
            cli_conn_free(conn);
            return 1;
        }
        send_prompt(conn->fd);

        cursor = newline + 1;
    }

    int remaining = (int)(conn->buf + conn->buf_len - cursor);
    if (remaining > 0 && cursor != conn->buf) {
        memmove(conn->buf, cursor, remaining);
    }
    conn->buf_len = remaining;
    conn->buf[conn->buf_len] = '\0';
    return 0;
}

// Non-blocking data handler for an existing CLI connection; drains until EAGAIN for EPOLLET
static void handle_cli_data(cli_conn_t *conn) {
    for (;;) {
        char tmp[256];
        ssize_t n = read(conn->fd, tmp, sizeof(tmp));
        if (n > 0) {
            int space = (int)sizeof(conn->buf) - conn->buf_len - 1;
            if (space <= 0) {
                cli_write(conn->fd, "Command line too long\n", 22);
                cli_conn_free(conn);
                return;
            }

            int copy_len = n < space ? (int)n : space;
            memcpy(conn->buf + conn->buf_len, tmp, copy_len);
            conn->buf_len += copy_len;
            conn->buf[conn->buf_len] = '\0';

            if (process_cli_buffer(conn)) {
                return;
            }
            if (copy_len < n) {
                cli_write(conn->fd, "Command line too long\n", 22);
                cli_conn_free(conn);
                return;
            }
            continue;
        }

        if (n == 0) {
            cli_conn_free(conn);
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }

        log_warn("read cli fd %d failed: %s", conn->fd, strerror(errno));
        cli_conn_free(conn);
        return;
    }
}

void cli_handle_connection(int fd) {
    if (fd == cli_sock) {
        for (;;) {
            int conn = accept(cli_sock, NULL, NULL);
            if (conn < 0) {
                if (errno == EINTR) {
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return;
                }
                log_err("accept cli conn failed: %s", strerror(errno));
                return;
            }

            log_info("New CLI connection accepted on fd %d", conn);

            if (make_socket_non_blocking(conn) < 0) {
                close(conn);
                continue;
            }

            cli_conn_t *state = calloc(1, sizeof(cli_conn_t));
            if (!state) {
                log_err("Failed to allocate CLI connection state");
                close(conn);
                continue;
            }
            state->fd = conn;
            state->buf_len = 0;
            state->next = cli_connections;
            cli_connections = state;

            struct epoll_event ev;
            ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
            ev.data.fd = conn;
            if (epoll_ctl(cli_epoll_fd, EPOLL_CTL_ADD, conn, &ev) < 0) {
                log_err("epoll_ctl add cli conn failed: %s", strerror(errno));
                cli_conn_free(state);
                continue;
            }

            send_welcome_prompt(conn);
        }
    }

    cli_conn_t *state = cli_conn_find(fd);
    if (state) {
        handle_cli_data(state);
    } else {
        log_warn("event for unknown CLI fd %d", fd);
        epoll_ctl(cli_epoll_fd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
    }
}
