#include "proto/client_server_common.pb.h"
#include "src/catalog_manager/headers/schmea_manager.hpp"
#include "src/query_manager/headers/parser.hpp"
#include "src/query_manager/headers/planner.hpp"
#include "src/server_helpers.hpp"
#include "src/transaction_manager/trasaction_manager.hpp"
#include <arpa/inet.h>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <termios.h>
#include <unistd.h>

static void wipe_storage_files(std::filesystem::path &heap_filepath, std::filesystem::path &index_filepath,
                               std::filesystem::path &schema_filepath) {

    std::ofstream file1(index_filepath, std::ios::binary | std::ios::out | std::ios::trunc);
    std::ofstream file2(heap_filepath, std::ios::binary | std::ios::out | std::ios::trunc);
    std::ofstream file3(schema_filepath, std::ios::binary | std::ios::out | std::ios::trunc);
    file1.close();
    file2.close();
    file3.close();
}

int main() {
    std::filesystem::path heap_filepath   = std::filesystem::current_path() / "heap.bin";
    std::filesystem::path index_filepath  = std::filesystem::current_path() / "index.bin";
    std::filesystem::path schema_filepath = std::filesystem::current_path() / "schema_file.bin";

    wipe_storage_files(heap_filepath, index_filepath, schema_filepath);

    int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    unlink(server::unix_server_path);

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, server::unix_server_path, sizeof(addr.sun_path) - 1);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, server::MAX_CLIENTS) < 0) {
        perror("listen");
        close(listen_fd);
        unlink(server::unix_server_path);
        return 1;
    }

    struct pollfd poll_table[server::MAX_CLIENTS];
    poll_table[0].fd      = listen_fd;
    poll_table[0].events  = POLLIN;
    poll_table[0].revents = 0;

    size_t nfds = 1;

    buffer_manager::buffer_pool    buff_pool(heap_filepath, index_filepath);
    access_methods::Access_methods access_methods;
    schema::schema_manager         sch_ma(schema_filepath);
    parser::Parser                 parser;

    transaction_manager::TransactionManager transaction_manager(sch_ma, parser, buff_pool, access_methods);

    while (1) {
        int ready_fds = poll(poll_table, nfds, 16);

        if (ready_fds > 0) {

            for (size_t fd = 0; fd < nfds; fd++) {

                if (!(poll_table[fd].revents & POLLIN))
                    continue;

                if (fd == 0) {
                    int new_client_fd = accept(listen_fd, NULL, NULL);

                    if (new_client_fd < 0)
                        continue;

                    if (nfds >= server::MAX_CLIENTS) {
                        close(new_client_fd);
                        continue;
                    }

                    poll_table[nfds].fd      = new_client_fd;
                    poll_table[nfds].events  = POLLIN;
                    poll_table[nfds].revents = 0;
                    nfds++;
                    continue;
                }

                uint32_t request_size_net;

                if (!server::recv_all(poll_table[fd].fd, &request_size_net, sizeof(request_size_net))) {
                    server::close_client(poll_table, &nfds, fd);
                    fd--;
                    continue;
                }

                size_t request_size = ntohl(request_size_net);

                if (request_size > server::MAX_CLIENT_MSG_SIZE) {
                    server::close_client(poll_table, &nfds, fd);
                    fd--;
                    continue;
                }

                std::string client_msg(request_size, '\0');

                if (!server::recv_all(poll_table[fd].fd, client_msg.data(), request_size)) {
                    server::close_client(poll_table, &nfds, fd);
                    fd--;
                    continue;
                }

                client_server_common::Request client_req;

                if (!client_req.ParseFromString(client_msg.c_str())) {
                    printf("ERROR : Parsing Client Message");
                    server::close_client(poll_table, &nfds, fd);
                    fd--;
                    continue;
                }

                worker_functions::client c = {static_cast<size_t>(poll_table[fd].fd), client_req};

                transaction_manager.IterateOrAddWorker(c);
                poll_table[fd] = poll_table[nfds - 1];
                nfds--;
                fd--;
            }
        } else if (ready_fds < 0) {
            throw std::runtime_error("SOME ERROR IN MAIN LOOP");
        }
    }

    write(STDOUT_FILENO, "\033[?25h", 6);
    close(listen_fd);
    unlink(server::unix_server_path);
    return 0;
}
