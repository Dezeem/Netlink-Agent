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

void cli_handle_connection(int fd) {
    if (fd == cli_sock) {
        int conn = accept(cli_sock, NULL, NULL);
        if (conn < 0) {
            log_err("accept cli conn failed: %s", strerror(errno));
            return;
        }
        // read simple command (blocking small read)
        char buf[256];
        int n = read(conn, buf, sizeof(buf)-1);
        if (n <= 0) {
            close(conn);
            return;
        }
        buf[n] = '\0';
        // only support "show interfaces\n" or "list\n"
        if (strncmp(buf, "show interfaces", 15) == 0 || strncmp(buf, "list", 4)==0) {
            // capture output by writing to socket
            // We'll duplicate behavior of list_interfaces to socket by simple approach: format lines
            // For simplicity call list_interfaces to stdout and also write minimal info
            // Better approach: iterate parser table and write formatted lines
            // We'll implement quick iteration by reusing /sys/class/net
            DIR *d = opendir("/sys/class/net");
            if (!d) {
                write(conn, "no interfaces\n", 14);
                close(conn);
                return;
            }
            struct dirent *de;
            char line[512];
            while ((de = readdir(d)) != NULL) {
                if (de->d_name[0] == '.') continue;
                iface_info_t *inf = get_iface_by_name(de->d_name);
                if (!inf) continue;
                int len = snprintf(line, sizeof(line), "%s up=%d rx=%lu tx=%lu rx_err=%lu tx_err=%lu\n",
                    inf->ifname, inf->up, inf->rx_bytes, inf->tx_bytes, inf->rx_err, inf->tx_err);
                write(conn, line, len);
            }
            closedir(d);
        } else {
            const char *resp = "unknown command\n";
            write(conn, resp, strlen(resp));
        }
        close(conn);
    }
}
