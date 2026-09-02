#pragma once

#include <cstddef>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace server {

constexpr size_t MAX_CLIENTS         = 10;
constexpr size_t MAX_CLIENT_MSG_SIZE = 4096;
constexpr char   unix_server_path[]  = "/tmp/db_scratch.sock";

bool send_all(int fd, const void *data, size_t size);

bool recv_all(int fd, void *data, size_t size);

void close_client(struct pollfd poll_table[MAX_CLIENTS], size_t *nfds, size_t fd);
} // namespace server
