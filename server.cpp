#include "proto/client_server_common.pb.h"
#include "src/catalog_manager/headers/schmea_manager.hpp"
#include "src/query_manager/headers/parser.hpp"
#include "src/query_manager/headers/planner.hpp"
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

// --- AI GENERATED HELPER FUNCTIONS (MAYBE CAN IMPROVE) ---
void fill_proto_schemas(client_server_common::Response         &response,
                        const std::vector<schema::schema_attr> &schemas) {

    for (const auto &schema : schemas) {
        auto *proto_schema = response.add_schemas();

        proto_schema->set_schema_name(schema.schema_name);

        for (const auto &table : schema.tables) {
            auto *proto_table = proto_schema->add_tables();

            proto_table->set_size(table.columns.size());
            proto_table->set_page_id(table.page_id);
            proto_table->set_table_name(table.table_name);

            for (const auto &column : table.columns) {
                auto *proto_column = proto_table->add_columns();

                proto_column->set_column_name(column.column_name);

                switch (column.column_type) {
                    case access_methods_types::STRING:
                        proto_column->set_column_type(
                            client_server_common::STRING);
                        break;

                    case access_methods_types::INTEGER:
                        proto_column->set_column_type(
                            client_server_common::INTEGER);
                        break;

                    case access_methods_types::FLOATING:
                        proto_column->set_column_type(
                            client_server_common::FLOAT);
                        break;

                    default:
                        proto_column->set_column_type(
                            client_server_common::UNKNOWN_COLUMN_TYPE);
                        break;
                }
            }
        }
    }
}
void fill_proto_results(
    client_server_common::Response                 &response,
    const std::vector<access_methods_types::row_t> &results) {

    for (const auto &row : results) {
        auto *proto_row = response.add_results();

        for (const auto &value : row.row) {
            auto *proto_value = proto_row->add_values();

            std::visit(
                [&](const auto &val) {
                    using T = std::decay_t<decltype(val)>;

                    if constexpr (std::is_same_v<T, std::string>) {
                        proto_value->set_string_value(val);

                    } else if constexpr (std::is_same_v<T, int>) {
                        proto_value->set_int_value(val);

                    } else if constexpr (std::is_same_v<T, float>) {
                        proto_value->set_float_value(val);
                    }
                },
                value);
        }
    }
}

client_server_common::Response
DB_Pipeline(schema::schema_manager &sch_ma, parser::Parser &parser,
            buffer_manager::buffer_pool    &buff_pool,
            access_methods::Access_methods &access_methods,
            client_server_common::Request  &input) {

    client_server_common::Response response_obj;
    index_write::root_struct       curr_root = {};

    if (input.first_load()) {
        response_obj.set_query_type(client_server_common::FIRST_LOAD);

        std::vector<schema::schema_attr> schemas;
        sch_ma.get_schema(schemas);

        fill_proto_schemas(response_obj, schemas);
        input.set_first_load(false);

        return response_obj;
    }

    parser::token_iterator tok_it(input.input());

    parser_types::ASTResult ast =
        parser.grammer_check(tok_it, sch_ma, input.schema_name());

    if (parser_types::SCHEMA_AST *sch_ptr =
            std::get_if<parser_types::SCHEMA_AST>(&ast)) {

        sch_ma.create_schema(*sch_ptr);

        response_obj.set_query_type(client_server_common::CREATE_SCHEMA_QUERY);

        std::vector<schema::schema_attr> schemas;
        sch_ma.get_schema(schemas);

        fill_proto_schemas(response_obj, schemas);

    } else if (parser_types::CREATE_TABLE_AST *ct_ast =
                   std::get_if<parser_types::CREATE_TABLE_AST>(&ast)) {

        if (input.schema_name() != "") {
            sch_ma.schema_create_table(input.schema_name(), *ct_ast);

            response_obj.set_query_type(
                client_server_common::CREATE_TABLE_QUERY);

            std::vector<schema::schema_attr> schemas;
            sch_ma.get_schema(schemas);

            fill_proto_schemas(response_obj, schemas);
        }

    } else if (parser_types::INSERT_AST *insert_ast =
                   std::get_if<parser_types::INSERT_AST>(&ast)) {

        if (input.schema_name() != "") {
            planner::insert_plan(buff_pool, access_methods, sch_ma, *insert_ast,
                                 curr_root, input.schema_name());

            response_obj.set_query_type(client_server_common::INSERT_QUERY);
        }

    } else if (parser_types::SELECT_AST *select_ast =
                   std::get_if<parser_types::SELECT_AST>(&ast)) {

        if (input.schema_name() != "") {
            response_obj.set_query_type(client_server_common::SELECT_QUERY);

            auto results =
                planner::select_plan(buff_pool, access_methods, sch_ma,
                                     *select_ast, input.schema_name());

            fill_proto_results(response_obj, results);

            std::vector<schema::schema_attr> schemas;
            sch_ma.get_schema(schemas);

            fill_proto_schemas(response_obj, schemas);
        }
    }

    return response_obj;
}

static bool send_all(int fd, const void *data, size_t size) {
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

static bool recv_all(int fd, void *data, size_t size) {
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

int main() {
    std::filesystem::path heap_filepath =
        std::filesystem::current_path() / "heap.bin";
    std::filesystem::path index_filepath =
        std::filesystem::current_path() / "index.bin";
    std::filesystem::path schema_filepath =
        std::filesystem::current_path() / "schema_file.bin";

    std::ofstream file1(index_filepath,
                        std::ios::binary | std::ios::out | std::ios::trunc);
    std::ofstream file2(heap_filepath,
                        std::ios::binary | std::ios::out | std::ios::trunc);
    std::ofstream file3(schema_filepath,
                        std::ios::binary | std::ios::out | std::ios::trunc);
    file1.close();
    file2.close();
    file3.close();

    buffer_manager::buffer_pool    buff_pool(heap_filepath, index_filepath);
    access_methods::Access_methods access_methods;
    schema::schema_manager         sch_ma(schema_filepath);
    parser::Parser                 parser;

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

                if (!recv_all(poll_table[fd].fd, &request_size_net,
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

                if (!recv_all(poll_table[fd].fd, client_msg.data(),
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

                if (!send_all(poll_table[fd].fd, &response_size_net,
                              sizeof(response_size_net)) ||
                    !send_all(poll_table[fd].fd, response_payload.data(),
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
