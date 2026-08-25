#pragma once

#include <sys/socket.h>

namespace server {

constexpr size_t MAX_CLIENTS         = 10;
constexpr size_t MAX_CLIENT_MSG_SIZE = 4096;
constexpr char   unix_server_path[]  = "/tmp/db_scratch.sock";

bool send_all(int fd, const void *data, size_t size);

bool recv_all(int fd, void *data, size_t size);

bool close_client(struct pollfd poll_table[server::MAX_CLIENTS], int fd);

} // namespace server
