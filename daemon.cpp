#include "client_server_common.hpp"
#include "src/catalog_manager/headers/schmea_manager.hpp"
#include "src/query_manager/headers/parser.hpp"
#include "src/query_manager/headers/planner.hpp"
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/poll.h>
#include <termios.h>
#include <unistd.h>
#include <variant>

constexpr size_t MAX_CLIENTS = 10;

client_server_common::Response
DB_Pipeline(std::string schema_name, schema::schema_manager &sch_ma,
            parser::Parser &parser, buffer_manager::buffer_pool &buff_pool,
            access_methods::Access_methods &access_methods,
            client_server_common::Request &input, bool first_load) {

    client_server_common::Response response_obj = {};
    index_write::root_struct       curr_root    = {};

    if (first_load) {
        response_obj.query_type = client_server_common::FIRST_LOAD;
        response_obj.schemas.emplace();
        sch_ma.get_schema(response_obj.schemas.value());
        return response_obj;
    }

    parser::token_iterator  tok_it(input);
    parser_types::ASTResult ast =
        parser.grammer_check(tok_it, sch_ma, schema_name);

    if (parser_types::SCHEMA_AST *sch_ptr =
            std::get_if<parser_types::SCHEMA_AST>(&ast)) {
        sch_ma.create_schema(*sch_ptr);
        response_obj.query_type = client_server_common::CREATE_SCHEMA_QUERY;
        response_obj.schemas.emplace();
        sch_ma.get_schema(response_obj.schemas.value());

    } else if (parser_types::CREATE_TABLE_AST *ct_ast =
                   std::get_if<parser_types::CREATE_TABLE_AST>(&ast)) {
        if (schema_name != "") {
            sch_ma.schema_create_table(schema_name, *ct_ast);
            response_obj.query_type = client_server_common::CREATE_TABLE_QUERY;
            response_obj.schemas.emplace();
            sch_ma.get_schema(response_obj.schemas.value());
        }
    } else if (parser_types::INSERT_AST *insert_ast =
                   std::get_if<parser_types::INSERT_AST>(&ast)) {
        if (schema_name != "") {
            planner::insert_plan(buff_pool, access_methods, sch_ma, *insert_ast,
                                 curr_root, schema_name);
            response_obj.query_type = client_server_common::INSERT_QUERY;
        }

    } else if (parser_types::SELECT_AST *select_ast =
                   std::get_if<parser_types::SELECT_AST>(&ast)) {
        if (schema_name != "") {
            response_obj.query_type = client_server_common::SELECT_QUERY;
            response_obj.results    = planner::select_plan(
                buff_pool, access_methods, sch_ma, *select_ast, schema_name);
            response_obj.schemas.emplace();
            sch_ma.get_schema(response_obj.schemas.value());
        }
    }

    return response_obj;
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

    struct pollfd stdin_poll = {STDIN_FILENO, POLLIN, 0};

    // can later have more (note this contains pointers to structs of pollfd)

    struct pollfd poll_table[MAX_CLIENTS];
    poll_table[0] = stdin_poll;

    while (1) {
        int ready_fds = poll(poll_table, MAX_CLIENTS, 16);

        if (ready_fds > 0) {

            for (int fd = 0; fd < MAX_CLIENTS; fd++) {
                if (poll_table[fd].revents & POLLIN) {

                    // thread here
                    DB_Pipeline(std::string schema_name, sch_ma, parser,
                                buff_pool, access_methods, Request & input,
                                bool first_load);
                }
            }
        } else if (ready_fds < 0) {
            throw std::runtime_error("SOME ERROR IN MAIN LOOP");
        }
    }
    write(STDOUT_FILENO, "\033[?25h", 6);
    return 0;
}
