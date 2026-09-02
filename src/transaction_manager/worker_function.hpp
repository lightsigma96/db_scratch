#pragma once

#include "../../proto/client_server_common.pb.h"
#include "../../src/catalog_manager/headers/schmea_manager.hpp"
#include "../../src/query_manager/headers/parser.hpp"
#include "../../src/query_manager/headers/planner.hpp"
#include "../../src/server_helpers.hpp"
#include "../../src/storage_manager/headers/types.hpp"
#include <arpa/inet.h>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <optional>
#include <thread>
#include <unistd.h>
#include <vector>

namespace transaction_manager {
class LockManager;
}

namespace worker_functions {

enum WORKER_STATE { IDLE, BUSY };
struct client {
    size_t                        fd;
    client_server_common::Request client_input;
};

struct Worker {
    uint8_t                      thread_id;
    std::optional<struct client> client;
    WORKER_STATE                 state = IDLE;
    std::thread                  thread;
    std::mutex                   mut;
    std::condition_variable      condvar;
};

static void fill_proto_schemas(client_server_common::Response &response, const std::vector<schema::schema_attr> &schemas) {
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
                        proto_column->set_column_type(client_server_common::STRING);
                        break;
                    case access_methods_types::INTEGER:
                        proto_column->set_column_type(client_server_common::INTEGER);
                        break;
                    case access_methods_types::FLOATING:
                        proto_column->set_column_type(client_server_common::FLOAT);
                        break;
                    default:
                        proto_column->set_column_type(client_server_common::UNKNOWN_COLUMN_TYPE);
                        break;
                }
            }
        }
    }
}

static void fill_proto_results(client_server_common::Response &response, const std::vector<access_methods_types::row_t> &results) {
    for (const auto &row : results) {
        auto *proto_row = response.add_results();
        for (const auto &value : row.row) {
            auto *proto_value = proto_row->add_values();
            std::visit(
                [&](const auto &val) {
                    using T = std::decay_t<decltype(val)>;
                    if constexpr (std::is_same_v<T, std::string>)
                        proto_value->set_string_value(val);
                    else if constexpr (std::is_same_v<T, int>)
                        proto_value->set_int_value(val);
                    else if constexpr (std::is_same_v<T, float>)
                        proto_value->set_float_value(val);
                },
                value);
        }
    }
}

static client_server_common::Response DB_Pipeline(schema::schema_manager &sch_ma, parser::Parser &parser,
                                                  buffer_manager::buffer_pool &buff_pool, access_methods::Access_methods &access_methods,
                                                  client_server_common::Request &input, uint8_t &transaction_id,
                                                  transaction_manager::LockManager &lock_manager) {
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
    parser::token_iterator  tok_it(input.input());
    parser_types::ASTResult ast = parser.grammer_check(tok_it, sch_ma, input.schema_name());
    if (auto *p = std::get_if<parser_types::SCHEMA_AST>(&ast)) {
        sch_ma.create_schema(*p);
        response_obj.set_query_type(client_server_common::CREATE_SCHEMA_QUERY);
        std::vector<schema::schema_attr> schemas;
        sch_ma.get_schema(schemas);
        fill_proto_schemas(response_obj, schemas);
    } else if (auto *p = std::get_if<parser_types::CREATE_TABLE_AST>(&ast)) {
        if (input.schema_name() != "") {
            sch_ma.schema_create_table(input.schema_name(), *p);
            response_obj.set_query_type(client_server_common::CREATE_TABLE_QUERY);
            std::vector<schema::schema_attr> schemas;
            sch_ma.get_schema(schemas);
            fill_proto_schemas(response_obj, schemas);
        }
    } else if (auto *p = std::get_if<parser_types::INSERT_AST>(&ast)) {
        if (input.schema_name() != "") {
            planner::insert_plan(buff_pool, access_methods, sch_ma, *p, curr_root, input.schema_name(), lock_manager);
            response_obj.set_query_type(client_server_common::INSERT_QUERY);
        }
    } else if (auto *p = std::get_if<parser_types::SELECT_AST>(&ast)) {
        if (input.schema_name() != "") {
            response_obj.set_query_type(client_server_common::SELECT_QUERY);
            auto results = planner::select_plan(buff_pool, access_methods, sch_ma, *p, input.schema_name(), transaction_id, lock_manager);
            fill_proto_results(response_obj, results);
            std::vector<schema::schema_attr> schemas;
            sch_ma.get_schema(schemas);
            fill_proto_schemas(response_obj, schemas);
        }
    }
    return response_obj;
}

inline void Worker(Worker &worker, schema::schema_manager &sch_ma, parser::Parser &parser, buffer_manager::buffer_pool &buff_pool,
                   access_methods::Access_methods &access_methods, transaction_manager::LockManager &lock_manager) {
    while (true) {

        worker.mut.lock();
        if (worker.state == IDLE) {
            std::unique_lock<std::mutex> lock_for_cond(worker.mut);
            worker.condvar.wait(lock_for_cond);
        }

        const int client_fd = static_cast<int>(worker.client->fd);
        auto      req       = worker.client->client_input;

        client_server_common::Response response =
            DB_Pipeline(sch_ma, parser, buff_pool, access_methods, req, worker.thread_id, lock_manager);

        std::string response_payload;
        if (!response.SerializeToString(&response_payload))
            printf("ERROR : Response Serialization");
        else {
            uint32_t response_size_net = htonl(static_cast<uint32_t>(response_payload.size()));
            if (!server::send_all(client_fd, &response_size_net, sizeof(response_size_net)) ||
                !server::send_all(client_fd, response_payload.data(), response_payload.size()))
                printf("ERROR : Client Response Send");
        }
        close(worker.client->fd);

        worker.mut.lock();
        worker.client = std::nullopt;
        worker.state  = IDLE;
        worker.mut.unlock();
    }
}

} // namespace worker_functions
