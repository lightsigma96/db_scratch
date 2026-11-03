#pragma once

#include "../../proto/client_server_common.pb.h"
#include "../../src/catalog_manager/headers/schmea_manager.hpp"
#include "../../src/query_manager/headers/parser.hpp"
#include "../../src/query_manager/headers/planner.hpp"
#include "../../src/server_helpers.hpp"
#include "../../src/storage_manager/headers/types.hpp"
#include "../../src/wal/wal.hpp"
#include <arpa/inet.h>
#include <cstdio>
#include <unistd.h>
#include <vector>

namespace transaction_manager {
class LockManager;
}

#include "worker_state.hpp"

namespace worker_functions {

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
                                                  client_server_common::Request &input, uint8_t &tid,
                                                  transaction_manager::LockManager &lock_manager, WAL::WAL &wal) {
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
            planner::plan_answer pa = planner::insert_plan(buff_pool, access_methods, sch_ma, *p, curr_root, input.schema_name(), wal);
            response_obj.set_query_type(client_server_common::INSERT_QUERY);
            response_obj.set_transaction_complete(pa.operation_complete);
        }
    } else if (auto *p = std::get_if<parser_types::SELECT_AST>(&ast)) {
        if (input.schema_name() != "") {
            response_obj.set_query_type(client_server_common::SELECT_QUERY);
            planner::plan_answer pa = planner::select_plan(buff_pool, access_methods, sch_ma, *p, input.schema_name(), tid, lock_manager);
            fill_proto_results(response_obj, pa.rows);
            response_obj.set_transaction_complete(pa.operation_complete);
            std::vector<schema::schema_attr> schemas;
            sch_ma.get_schema(schemas);
            fill_proto_schemas(response_obj, schemas);
        }
    }
    return response_obj;
}

inline void Worker(Worker &worker, schema::schema_manager &sch_ma, parser::Parser &parser, buffer_manager::buffer_pool &buff_pool,
                   access_methods::Access_methods &access_methods, transaction_manager::LockManager &lock_manager, WAL::WAL &wal) {
    while (true) {

        std::unique_lock<std::mutex> lock(worker.mut);

        if (!worker.wait_for_request(lock))
            return;

        const int client_fd = static_cast<int>(worker.client->fd);
        auto      req       = worker.client->client_input;
        lock.unlock();

        uint8_t tid = lock_manager.get_transaction_id();
        client_server_common::Response response;
        try {
            response = DB_Pipeline(sch_ma, parser, buff_pool, access_methods,
                                   req, tid, lock_manager, wal);
        } catch (...) {
            // Keep the worker alive and release any locks acquired before the
            // failed statement.
            response.set_query_type(client_server_common::UNKNOWN);
            response.set_transaction_complete(false);
        }
        lock_manager.ReleaseLockFromLockTable(tid);

        std::string response_payload;
        if (!response.SerializeToString(&response_payload))
            printf("ERROR : Response Serialization");
        else {
            uint32_t response_size_net = htonl(static_cast<uint32_t>(response_payload.size()));
            if (!server::send_all(client_fd, &response_size_net, sizeof(response_size_net)) ||
                !server::send_all(client_fd, response_payload.data(), response_payload.size()))
                printf("ERROR : Client Response Send");
        }
        close(client_fd);

        lock.lock();
        worker.client = std::nullopt;
        worker.state  = IDLE;
        lock.unlock();
        worker.condvar.notify_all();
    }
}

} // namespace worker_functions
