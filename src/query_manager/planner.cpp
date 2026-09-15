#include "headers/planner.hpp"
#include "../transaction_manager/trasaction_manager.hpp"
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

planner::plan_answer planner::select_plan(buffer_manager::buffer_pool &buff_pool, access_methods::Access_methods &access_methods,
                                          schema::schema_manager &sch_man, parser_types::SELECT_AST &ast, std::string schema_name,
                                          uint8_t &tid, transaction_manager::LockManager &lock_manager) {

    std::vector<access_methods_types::row_t>        matched_rows;
    std::optional<std::vector<schema::ENTITY_TYPE>> table_find = sch_man.entity_find(schema::TABLE, ast.table_name, schema_name);
    schema::tables_attrs                           *table_ptr  = nullptr;
    if (table_find.has_value()) {
        if (!(table_ptr = std::get_if<schema::tables_attrs>(&table_find.value()[0])))
            throw std::runtime_error("ERROR AT GETTING TABLE NAME FOR PLANNER");
    }
    std::vector<size_t>                                     data_size_arr;
    std::vector<access_methods_types::SUPORTED_COLUMN_TYPE> col_types;
    if (!table_ptr)
        throw std::runtime_error("ERROR SETTING TABLE, BUT FOUND");
    for (const auto &ele : table_ptr->columns) {
        col_types.push_back(ele.column_type);
        switch (ele.column_type) {
            case access_methods_types::STRING:
                data_size_arr.push_back(access_methods_types::STRING_MAX_SIZE);
                break;
            case access_methods_types::INTEGER:
                data_size_arr.push_back(sizeof(int));
                break;
            case access_methods_types::FLOATING:
                data_size_arr.push_back(sizeof(float));
                break;
        }
    }

    // WARN : filter and projection should also get tid & lock_manager & should be propagated across every operation, but here taking a
    // short path directly to seq_scan
    auto seq_scan = std::make_unique<Seq_scan>(*table_ptr, access_methods, buff_pool, data_size_arr, col_types, tid, lock_manager);
    seq_scan->init();
    auto filter = std::make_unique<Filter>(*table_ptr, *seq_scan, ast.have_predicate, ast.predicate);
    filter->init();
    auto project = std::make_unique<Projection>(*table_ptr, *filter, ast);
    project->init();

    access_methods_types::ScanResult row     = project->next();
    bool                             op_comp = false;
    while (true) {
        if (row.scan_status == access_methods_types::SUCCESS) {
            matched_rows.push_back(row.scan_result.value());
            row     = project->next();
            op_comp = row.opration_complete;
        } else if (row.scan_status == access_methods_types::ERR) {
            throw std::runtime_error("SOME ERROR OCCURED WHILE SCANING ROWS");
        } else {
            break;
        }
    }

    return {matched_rows, op_comp, {}};
}

planner::plan_answer planner::insert_plan(buffer_manager::buffer_pool &buff_pool, access_methods::Access_methods &access_methods,
                                          schema::schema_manager &sch_man, parser_types::INSERT_AST &ast,
                                          index_write::root_struct &curr_root, std::string schema_name) {

    std::vector<access_methods_types::row_t>        inserted_rows;
    std::optional<std::vector<schema::ENTITY_TYPE>> table_find = sch_man.entity_find(schema::TABLE, ast.table_name, schema_name);
    schema::tables_attrs                           *table_ptr  = nullptr;
    if (table_find.has_value()) {
        if (!(table_ptr = std::get_if<schema::tables_attrs>(&table_find.value()[0])))
            throw std::runtime_error("ERROR AT GETTING TABLE NAME FOR PLANNER");
    }
    if (!table_ptr)
        throw std::runtime_error("ERROR SETTING TABLE, BUT FOUND");

    std::vector<size_t> data_size_arr;
    for (const auto &ele : table_ptr->columns) {
        switch (ele.column_type) {
            case access_methods_types::STRING:
                data_size_arr.push_back(access_methods_types::STRING_MAX_SIZE);
                break;
            case access_methods_types::INTEGER:
                data_size_arr.push_back(sizeof(int));
                break;
            case access_methods_types::FLOATING:
                data_size_arr.push_back(sizeof(float));
                break;
        }
    }

    auto insert = std::make_unique<Insert>(*table_ptr, nullptr, ast, access_methods, buff_pool, curr_root, data_size_arr);
    access_methods_types::ScanResult row = insert->next();

    bool op_comp = false;
    while (true) {
        if (row.scan_status == access_methods_types::SUCCESS) {
            inserted_rows.push_back(row.scan_result.value());
            row     = insert->next();
            op_comp = row.opration_complete;
        } else if (row.scan_status == access_methods_types::ERR) {
            throw std::runtime_error("SOME ERROR OCCURED WHILE SCANING ROWS");
        } else {
            break;
        }
    }

    return {inserted_rows, op_comp, insert->get_inserted_rids()};
}
