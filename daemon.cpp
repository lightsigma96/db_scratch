#include "proto/client_server_common.pb.h"
#include "src/catalog_manager/headers/schmea_manager.hpp"
#include "src/query_manager/headers/parser.hpp"
#include "src/query_manager/headers/planner.hpp"
#include "src/server.hpp"
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
#include <type_traits>
#include <unistd.h>
#include <variant>

constexpr size_t MAX_CLIENTS         = 10;
constexpr size_t MAX_CLIENT_MSG_SIZE = 4096;
constexpr char   unix_server_path[]  = "/tmp/db_scratch.sock";

static void wipe_storage_files(std::filesystem::path &heap_filepath,
                               std::filesystem::path &index_filepath,
                               std::filesystem::path &schema_filepath) {

    std::ofstream file1(index_filepath,
                        std::ios::binary | std::ios::out | std::ios::trunc);
    std::ofstream file2(heap_filepath,
                        std::ios::binary | std::ios::out | std::ios::trunc);
    std::ofstream file3(schema_filepath,
                        std::ios::binary | std::ios::out | std::ios::trunc);
    file1.close();
    file2.close();
    file3.close();
}

int main() {
    std::filesystem::path heap_filepath =
        std::filesystem::current_path() / "heap.bin";
    std::filesystem::path index_filepath =
        std::filesystem::current_path() / "index.bin";
    std::filesystem::path schema_filepath =
        std::filesystem::current_path() / "schema_file.bin";

    wipe_storage_files(heap_filepath, index_filepath, schema_filepath);

    buffer_manager::buffer_pool    buff_pool(heap_filepath, index_filepath);
    access_methods::Access_methods access_methods;
    schema::schema_manager         sch_ma(schema_filepath);
    parser::Parser                 parser;
    transaction_manager::TransactionManager transaction_manager(
        sch_ma, parser, buff_pool, access_methods);

    int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    unlink(unix_server_path);

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, unix_server_path, sizeof(addr.sun_path) - 1);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        close(listen_fd);
        unlink(unix_server_path);
        return 1;
    }

    struct pollfd poll_table[MAX_CLIENTS];
    poll_table[0].fd      = listen_fd;
    poll_table[0].events  = POLLIN;
    poll_table[0].revents = 0;

    size_t nfds = 1;

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

                    if (nfds >= MAX_CLIENTS) {
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

                if (!server::recv_all(poll_table[fd].fd, &request_size_net,
                                      sizeof(request_size_net))) {
                    close(poll_table[fd].fd);
                    poll_table[fd] = poll_table[nfds - 1];
                    nfds--;
                    fd--;
                    continue;
                }

                size_t request_size = ntohl(request_size_net);

                if (request_size > MAX_CLIENT_MSG_SIZE) {
                    close(poll_table[fd].fd);
                    poll_table[fd] = poll_table[nfds - 1];
                    nfds--;
                    fd--;
                    continue;
                }

                std::string client_msg(request_size, '\0');

                if (!server::recv_all(poll_table[fd].fd, client_msg.data(),
                                      request_size)) {
                    close(poll_table[fd].fd);
                    poll_table[fd] = poll_table[nfds - 1];
                    nfds--;
                    fd--;
                    continue;
                }

                client_server_common::Request client_req;

                if (!client_req.ParseFromString(client_msg)) {
                    printf("ERROR : Parsing Client Message");
                    close(poll_table[fd].fd);
                    poll_table[fd] = poll_table[nfds - 1];
                    nfds--;
                    fd--;
                    continue;
                }

                // add job to worker if ideal else create a new one and then
                // transfer client along with ownership
                client_server_common::Response response = DB_Pipeline(
                    sch_ma, parser, buff_pool, access_methods, client_req);

                std::string response_payload;

                if (!response.SerializeToString(&response_payload)) {
                    printf("ERROR : Response Serialization");
                    close(poll_table[fd].fd);
                    poll_table[fd] = poll_table[nfds - 1];
                    nfds--;
                    fd--;
                    continue;
                }

                uint32_t response_size_net =
                    htonl(static_cast<uint32_t>(response_payload.size()));

                if (!server::send_all(poll_table[fd].fd, &response_size_net,
                                      sizeof(response_size_net)) ||
                    !server::send_all(poll_table[fd].fd,
                                      response_payload.data(),
                                      response_payload.size())) {
                    close(poll_table[fd].fd);
                    poll_table[fd] = poll_table[nfds - 1];
                    nfds--;
                    fd--;
                    continue;
                }

                close(poll_table[fd].fd);
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
    unlink(unix_server_path);
    return 0;
}
