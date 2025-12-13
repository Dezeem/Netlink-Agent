#ifndef CLI_H
#define CLI_H

int cli_start(int epoll_fd);
void cli_handle_connection(int fd);

#endif
