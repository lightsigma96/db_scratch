#ifndef SCHMEA_BUILD
#define SCHMEA_BUILD

#include "../../query_manager/headers/types.hpp"
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unistd.h>
#include <variant>
#include <vector>

namespace schema {

#pragma pack(push, 1)
struct col_attrs {
    std::string                                column_name;
    access_methods_types::SUPORTED_COLUMN_TYPE column_type;

    bool col_type_match(access_methods_types::SUPORTED_COLUMN_TYPE col_type,
                        std::string value) const {
        size_t idx = 0;
        if (col_type == access_methods_types::STRING) {
            return true;
        } else if (col_type == access_methods_types::INTEGER) {
            std::stoi(value, &idx);
            return idx == value.size();
        } else if (col_type == access_methods_types::FLOATING) {
            std::stod(value, &idx);
            return idx == value.size();
        }
        return false;
    }
};
#pragma pack(pop)

#pragma pack(push, 1)
struct tables_attrs {
    size_t                   heap_page_offset = 0;
    heap_page_types::page_id page_id          = 0;
    std::string              table_name;
    std::vector<col_attrs>   columns;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct schema_attr {
    std::string               schema_name;
    std::vector<tables_attrs> tables;
};
#pragma pack(pop)

enum FIND_TYPE { SCHEMA, TABLE, COLUMN };

using ENTITY_TYPE = std::variant<schema_attr, tables_attrs, col_attrs>;

class schema_manager {
  private:
    std::filesystem::path    schmea_table_path;
    FILE                    *schmea_file;
    size_t                   schema_file_size;
    void                     deserialize();
    void                     serialize();
    std::vector<schema_attr> schema_catalog;

  public:
    schema_manager(const std::filesystem::path &schmea_table_path)
        : schmea_table_path(schmea_table_path) {
        schmea_file = fopen(schmea_table_path.c_str(), "ab+");

        if (!schmea_file) {
            throw std::runtime_error("ERROR CREATING SCHEMA FILE");
        }

        fseek(schmea_file, 0, SEEK_END);
        schema_file_size = ftell(schmea_file);
        fseek(schmea_file, 0, SEEK_SET);

        if (schema_file_size > 0) // else handle for emtpy files
            deserialize();
    };

    void create_schema(parser_types::SCHEMA_AST &ast);
    void schema_create_table(const std::string              &schema_name,
                             parser_types::CREATE_TABLE_AST &ast);

    std::optional<std::vector<ENTITY_TYPE>> entity_find(
        FIND_TYPE find, const std::string &child_context,
        std::optional<std::string_view> parent_context       = std::nullopt,
        std::optional<std::string_view> grand_parent_context = std::nullopt) {

        std::vector<ENTITY_TYPE> res;
        switch (find) {
            case SCHEMA:
                for (size_t i = 0; i < schema_catalog.size(); i++) {
                    if (child_context == schema_catalog[i].schema_name) {
                        res.push_back(schema_catalog[i]);
                        return res;
                    }
                }
                return std::nullopt;
                break;

            case TABLE:
                if (parent_context.has_value()) {

                    std::optional<std::vector<ENTITY_TYPE>> schema_find =
                        entity_find(SCHEMA,
                                    std::string(parent_context.value()));
                    if (schema_find.has_value() && schema_find->size() == 1) {
                        if (auto *curr_schema = std::get_if<schema_attr>(
                                &schema_find.value()[0])) {
                            for (int k = 0; k < curr_schema->tables.size();
                                 k++) {
                                if (child_context ==
                                    curr_schema->tables[k].table_name) {
                                    res.push_back(curr_schema->tables[k]);
                                    return res;
                                }
                            }
                        }
                    }
                }
                return std::nullopt;
                break;

            case COLUMN:
                if (grand_parent_context.has_value() &&
                    parent_context.has_value()) {
                    std::optional<std::vector<ENTITY_TYPE>> schema_find =
                        entity_find(SCHEMA,
                                    std::string(grand_parent_context.value()));

                    std::optional<std::vector<ENTITY_TYPE>> table_find =
                        entity_find(TABLE, std::string(parent_context.value()),
                                    std::string(grand_parent_context.value()));

                    if (schema_find.has_value() && table_find.has_value()) {
                        if (!table_find->empty()) {
                            if (tables_attrs *curr_table =
                                    std::get_if<tables_attrs>(
                                        &table_find.value()[0])) {
                                if (child_context == "*") {
                                    for (size_t z = 0;
                                         z < curr_table->columns.size(); z++) {
                                        res.push_back(curr_table->columns[z]);
                                    }
                                    return res;
                                } else {
                                    for (size_t z = 0;
                                         z < curr_table->columns.size(); z++) {
                                        if (child_context ==
                                            curr_table->columns[z]
                                                .column_name) {
                                            res.push_back(
                                                curr_table->columns[z]);
                                            return res;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                return std::nullopt;

            default:
                throw std::runtime_error("ERROR FINDING ENTITY");
        }
    };
    void get_schema(std::vector<schema_attr> &schemas) const {
        for (const schema_attr &schema : schema_catalog) {
            std::vector<tables_attrs> tb;
            for (const tables_attrs &table : schema.tables) {
                tb.push_back(table);
            }
            schemas.push_back({schema.schema_name, tb});
        }
    }
    void iterate_catalog() {
        for (schema_attr &sch : schema_catalog) {
            std::cout << "Schema : " << sch.schema_name << "\n";
            for (tables_attrs &t : sch.tables) {
                std::cout << "Table : " << t.table_name << "\n";
            }
        }
    }

    ~schema_manager() {
        // write schema here, before closing file
        serialize();
        std::string message = "\nSCHEMA FILE WRITTEN\n";
        write(STDOUT_FILENO, message.data(), message.size());
        fclose(schmea_file);
        fflush(schmea_file);
    }
};

} // namespace schema

#endif
