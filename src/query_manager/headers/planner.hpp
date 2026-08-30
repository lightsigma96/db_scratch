#ifndef PLANNER
#define PLANNER

#include "../../../src/transaction_manager/trasaction_manager.hpp"
#include "../../catalog_manager/headers/schmea_manager.hpp"
#include "../../storage_manager/headers/access_methods.hpp"
#include "../../storage_manager/headers/insert.hpp"
#include "../../storage_manager/headers/types.hpp"
#include "types.hpp"
#include <atomic>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace planner {

class Operator {
  protected:
  public:
    Operator() {
    }
    virtual access_methods_types::ScanResult next()  = 0;
    virtual void                             init()  = 0;
    virtual void                             close() = 0;
};

class Seq_scan : public Operator {
  protected:
    access_methods::Access_methods                          &am;
    buffer_manager::buffer_pool                             &buff_pool;
    access_methods::Access_methods::heap_scan                heap_scan;
    std::vector<size_t>                                     &data_size_arr;
    std::vector<access_methods_types::SUPORTED_COLUMN_TYPE> &col_types;
    schema::tables_attrs                                     table_attr;
    transaction_manager::LockManager                        &lock_manager;
    const uint8_t                                           &thread_id;
    // specifies this is the last op

  public:
    Seq_scan(const schema::tables_attrs tn, access_methods::Access_methods &access_methods, buffer_manager::buffer_pool &buff_pool,
             std::vector<size_t> &data_size_arr, std::vector<access_methods_types::SUPORTED_COLUMN_TYPE> &col_types,
             const uint8_t &thread_id, transaction_manager::LockManager &lock_manager)
        : am(access_methods), buff_pool(buff_pool), heap_scan(buff_pool), data_size_arr(data_size_arr), col_types(col_types),
          table_attr(tn), lock_manager(lock_manager), thread_id(thread_id) {
    }

    void init() override {
    }

    access_methods_types::ScanResult next() override {
        // calls heap scan for next row, and return

        access_methods_types::ScanResult res = heap_scan.scan(thread_id, data_size_arr, col_types, lock_manager);

        if (res.scan_status == access_methods_types::ERR) {
            return {access_methods_types::ERR, std::nullopt, 0};
        } else if (res.scan_status == access_methods_types::EOP) {
            // add logic to handle next page corresponding to this table
            return {access_methods_types::EOP, std::nullopt, 0};
        } else if (res.scan_status == access_methods_types::EOPs) {
            return {access_methods_types::EOPs, std::nullopt, 0};
        }
        return {access_methods_types::SUCCESS, res.scan_result.value(), res.tid};
    }

    void close() override {
    }
};

class Filter : public Operator {
  protected:
    parser_types::Predicate                   &predicate;
    size_t                                     search_column = 0;
    access_methods_types::SUPORTED_COLUMN_TYPE col_type;
    schema::tables_attrs                       table_attr;

    access_methods_types::SARG parse_predicate(const access_methods_types::SUPORTED_COLUMN_TYPE &col_type) {
        access_methods_types::SARG ret_sarg;
        try {
            if (predicate.op == "==") {
                ret_sarg.op = access_methods_types::EQ;
            } else if (predicate.op == ">=") {
                ret_sarg.op = access_methods_types::GTE;
            } else if (predicate.op == "<=") {
                ret_sarg.op = access_methods_types::LSE;
            } else if (predicate.op == ">") {
                ret_sarg.op = access_methods_types::GT;
            } else if (predicate.op == "<") {
                ret_sarg.op = access_methods_types::LS;
            }
            ret_sarg.col      = predicate.col;
            ret_sarg.constant = convert_correct_type(predicate.value, col_type);
            return ret_sarg;
        } catch (...) {
            throw;
        }
    };
    access_methods_types::SARG to_match;
    std::optional<bool>        match_sarg(std::optional<access_methods_types::row_t> res_row, size_t row_no) {
        if (!res_row.has_value())
            return std::nullopt;

        switch (to_match.op) {
            case access_methods_types::EQ:
                return (res_row.value().row[row_no] == to_match.constant);
            case access_methods_types::GT:
                return (res_row.value().row[row_no] > to_match.constant);
            case access_methods_types::GTE:
                return (res_row.value().row[row_no] >= to_match.constant);
            case access_methods_types::LS: // Data is Less Than Constant
                return (res_row.value().row[row_no] < to_match.constant);
            case access_methods_types::LSE: // Data is Less Than or Equal to
                                            // Constant
                return (res_row.value().row[row_no] <= to_match.constant);
            default:
                return false;
        }
    }
    Operator &next_op;
    bool      have_predicate;

  public:
    Filter(const schema::tables_attrs tn, Operator &op, bool have_predicate, parser_types::Predicate &predicate)
        : next_op(op), predicate(predicate), table_attr(tn), have_predicate(have_predicate) {};
    void init() override {
        if (have_predicate) {

            bool found = false;
            for (int i = 0; i < table_attr.columns.size(); i++) {
                if (table_attr.columns[i].column_name == predicate.col) {
                    search_column = i;
                    col_type      = table_attr.columns[i].column_type;
                    found         = true;
                    break;
                }
            }
            if (!found)
                throw std::runtime_error("Column not found");

            to_match = parse_predicate(col_type);
        }
    };

    access_methods_types::ScanResult next() override {
        /* Checks SARGs */
        while (true) {
            access_methods_types::ScanResult res_row = this->next_op.next();
            if (res_row.scan_status == access_methods_types::EOP) {
                return {access_methods_types::EOPs, std::nullopt, 0};
            } else if (res_row.scan_status == access_methods_types::EOPs) {
                return {access_methods_types::EOPs, std::nullopt, 0};
            } else if (res_row.scan_status == access_methods_types::ERR) {
                return {access_methods_types::ERR, std::nullopt, 0};
            }
            if (res_row.scan_result.has_value()) {
                if (have_predicate && match_sarg(res_row.scan_result.value(), search_column).value()) {
                    return res_row;
                }
                if (!have_predicate) {
                    return res_row;
                }
            }
        }
    };

    void close() override {
    }
};

class Projection : public Operator {
  protected:
    Operator                 &next_op;
    parser_types::SELECT_AST &ast;
    std::vector<int>          original_idx;
    schema::tables_attrs      table_attr;

  public:
    Projection(schema::tables_attrs tn, Operator &op, parser_types::SELECT_AST &ast) : next_op(op), ast(ast), table_attr(tn) {
    }

    void init() override {
        for (const std::string &to_project_col : ast.cols_name) {
            for (int i = 0; i < table_attr.columns.size(); i++) {
                if (table_attr.columns[i].column_name == to_project_col) {
                    original_idx.push_back(i);
                }
            }
        }
    };

    access_methods_types::ScanResult next() override {

        access_methods_types::row_t      projected_row;
        access_methods_types::ScanResult res_row = this->next_op.next();

        if (res_row.scan_status == access_methods_types::EOP) {
            return {access_methods_types::EOP, std::nullopt, 0};
        } else if (res_row.scan_status == access_methods_types::EOPs) {
            return {access_methods_types::EOPs, std::nullopt, 0};
        } else if (res_row.scan_status == access_methods_types::ERR) {
            return {access_methods_types::ERR, std::nullopt, 0};
        }
        for (const int &idx : original_idx) {
            projected_row.row.push_back(res_row.scan_result.value().row[idx]);
        }

        return {access_methods_types::SUCCESS, projected_row};
    }
    void close() override {
    }
};

class Insert : public Operator {
  private:
    Operator                         *next_op;
    std::vector<size_t>               data_size_arr;
    parser_types::INSERT_AST         &ast;
    access_methods::Access_methods   &am;
    buffer_manager::buffer_pool      &buff_pool;
    index_write::root_struct         &curr_root;
    std::atomic<int>                  curr_row = 0;
    transaction_manager::LockManager &lock_manager;

  public:
    Insert(schema::tables_attrs &tn, Operator *next_op, parser_types::INSERT_AST &ast, access_methods::Access_methods &am,
           buffer_manager::buffer_pool &buff_pool, index_write::root_struct &curr_root, std::vector<size_t> data_size_arr,
           transaction_manager::LockManager &lock_manager)
        : next_op(next_op), ast(ast), am(am), buff_pool(buff_pool), curr_root(curr_root), data_size_arr(data_size_arr),
          lock_manager(lock_manager) {};

    void init() override {
    }

    access_methods_types::ScanResult next() override {
        access_methods_types::row_t inserted_row;
        if (curr_row < ast.values.size()) {
            if (auto v = insert::create_entry(buff_pool, am, ast.values[curr_row], data_size_arr, &curr_root, false, lock_manager)) {
                // curr_root.root_pid = v.value(); // returns nullopt if index
                // is not set
            }
            curr_row++;
            return {access_methods_types::SUCCESS, inserted_row, 0};
        } else if (curr_row == ast.values.size()) {
            return {access_methods_types::EOP, std::nullopt, 0};
        }
        return {access_methods_types::ERR, std::nullopt, 0};
    }
    void close() override {
    }
};

std::vector<access_methods_types::row_t> select_plan(buffer_manager::buffer_pool &buff_pool, access_methods::Access_methods &access_methods,
                                                     schema::schema_manager &sch_man, parser_types::SELECT_AST &ast,
                                                     std::string schema_name, uint8_t &thread_id,
                                                     transaction_manager::LockManager &lock_manager);

std::vector<access_methods_types::row_t> insert_plan(buffer_manager::buffer_pool &buff_pool, access_methods::Access_methods &access_methods,
                                                     schema::schema_manager &sch_man, parser_types::INSERT_AST &ast,
                                                     index_write::root_struct &curr_root, std::string schema_name);

}; // namespace planner

#endif
