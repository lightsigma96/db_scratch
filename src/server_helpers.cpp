#include "server_helpers.hpp"

bool server::send_all(int fd, const void *data, size_t size) {
    const char *buffer = static_cast<const char *>(data);
    size_t      sent   = 0;

    while (sent < size) {
        ssize_t n = send(fd, buffer + sent, size - sent, 0);

        if (n <= 0)
            return false;

        sent += n;
    }

    return true;
}

bool server::recv_all(int fd, void *data, size_t size) {
    char  *buffer   = static_cast<char *>(data);
    size_t received = 0;

    while (received < size) {
        ssize_t n = recv(fd, buffer + received, size - received, 0);

        if (n <= 0)
            return false;

        received += n;
    }

    return true;
}

void server::close_client(struct pollfd poll_table[MAX_CLIENTS], size_t *nfds, size_t fd) {
    close(poll_table[fd].fd);
    poll_table[fd] = poll_table[*nfds - 1];
    (*nfds)--;
}
