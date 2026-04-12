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

static int make_socket_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) == -1) return -1;
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
        return -1;
    }
    if (listen(cli_sock, 5) < 0) {
        log_err("cli listen failed: %s", strerror(errno));
        close(cli_sock);
        return -1;
    }
    if (make_socket_non_blocking(cli_sock) < 0) {
        log_warn("could not make cli_sock non blocking");
    }
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = cli_sock;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, cli_sock, &ev) < 0) {
        log_err("epoll_ctl add cli_sock failed: %s", strerror(errno));
        close(cli_sock);
        return -1;
    }
    log_info("cli socket listening at %s", CLI_SOCKET_PATH);
    return cli_sock;
}

// Structure to track active CLI connections
typedef struct {
    int fd;
    int epoll_fd;
} cli_connection_t;

// Send welcome message and prompt
static void send_welcome_prompt(int conn) {
    const char *welcome = "=== Netlink Agent CLI ===\n"
                         "Available commands:\n"
                         "  show interfaces, list - Display interface status\n"
                         "  help - Show this help message\n"
                         "  quit, exit - Close connection\n"
                         "\n> ";
    write(conn, welcome, strlen(welcome));
}

// Send command prompt
static void send_prompt(int conn) {
    const char *prompt = "> ";
    write(conn, prompt, strlen(prompt));
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
        write(conn, line, len);

        // Send interface details using thread-safe copy
        iface_info_t *safe_list = get_iface_list_safe();
        if (!safe_list) {
            write(conn, "Error: Failed to get interface list\n", 35);
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
            write(conn, line, len);

            // Send IP addresses
            if (inf->addr_cnt > 0) {
                len = snprintf(line, sizeof(line), "  Addresses (%d):\n", inf->addr_cnt);
                write(conn, line, len);
                
                for (int i = 0; i < inf->addr_cnt; i++) {
                    len = snprintf(line, sizeof(line),
                        "    [%d] %s/%d (%s)\n",
                        i + 1,
                        inf->addrs[i].addr,
                        inf->addrs[i].prefixlen,
                        inf->addrs[i].family == AF_INET ? "IPv4" : "IPv6");
                    write(conn, line, len);
                }
            } else {
                write(conn, "  No addresses\n", 15);
            }
            
            write(conn, "\n", 1);
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
        write(conn, help, strlen(help));
        return 0; // Continue session
    }
    else if (strncmp(command, "quit", 4) == 0 || strncmp(command, "exit", 4) == 0) {
        const char *goodbye = "Goodbye!\n";
        write(conn, goodbye, strlen(goodbye));
        return 1; // End session
    }
    else {
        const char *resp = "Unknown command. Type 'help' for available commands.\n";
        write(conn, resp, strlen(resp));
        return 0; // Continue session
    }
}

// Handle CLI connection with interactive command loop
static void handle_cli_session(int conn, int epoll_fd) {
    (void)epoll_fd; // Unused parameter
    send_welcome_prompt(conn);
    
    char buf[256];
    int session_active = 1;
    
    while (session_active) {
        // Read command from client
        int n = read(conn, buf, sizeof(buf)-1);
        if (n <= 0) {
            // Connection closed or error
            break;
        }
        
        buf[n] = '\0';
        
        // Remove newline characters
        char *newline = strchr(buf, '\n');
        if (newline) *newline = '\0';
        newline = strchr(buf, '\r');
        if (newline) *newline = '\0';
        
        // Skip empty commands
        if (strlen(buf) == 0) {
            send_prompt(conn);
            continue;
        }
        
        // Handle command
        int should_exit = handle_command(conn, buf);
        
        if (should_exit) {
            session_active = 0;
        } else {
            send_prompt(conn);
        }
    }
    
    close(conn);
    log_info("CLI session ended for fd %d", conn);
}

void cli_handle_connection(int fd) {
    if (fd == cli_sock) {
        // New connection request
        int conn = accept(cli_sock, NULL, NULL);
        if (conn < 0) {
            log_err("accept cli conn failed: %s", strerror(errno));
            return;
        }
        
        log_info("New CLI connection accepted on fd %d", conn);
        
        // Handle the CLI session (this will block until session ends)
        handle_cli_session(conn, -1); // -1 indicates no epoll registration needed
    }
}
