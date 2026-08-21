#include "headers/parser.hpp"
#include "headers/lexer.hpp"
#include "headers/types.hpp"
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

/* Token Iterator Definitions */

parser::token_iterator::token_iterator(const std::string &query) : curr_idx(0) {
    tokens = lexer::lexer(query);
};

lexer_types::Token parser::token_iterator::get_next() {
    if (curr_idx < tokens.size()) {
        return tokens[curr_idx++];
    } else {
        throw std::runtime_error("TOKENS OUT OF BOUNDS (POSITIVE)");
    }
}
lexer_types::Token parser::token_iterator::get_prev() {

    if (curr_idx > 0) {
        return tokens[curr_idx - 1];
    } else {
        throw std::runtime_error("TOKENS OUT OF BOUNDS (NEGATIVE)");
    }
}

bool parser::token_iterator::has_next() {
    if (curr_idx < tokens.size()) {
        return true;
    }
    return false;
}

lexer_types::Token parser::token_iterator::peek(int idx) const {

    int pos = curr_idx + idx - 1;

    if (pos >= 0 && pos < tokens.size()) {
        return tokens[pos];
    }

    throw std::runtime_error("COULD NOT PEEK, INVALID INDEX");
}

/* Parser Definitions */

std::string
parser::Parser::parse_from_clause(parser::token_iterator &tok_it,
                                  schema::schema_manager &schema_manager,
                                  const std::string      &schema_name) {
    lexer_types::Token sub_token = tok_it.get_next();
    std::optional<std::vector<schema::ENTITY_TYPE>> table_find =
        schema_manager.entity_find(schema::TABLE, sub_token.token_value,
                                   schema_name);
    if (table_find.has_value()) {
        return sub_token.token_value;
    }
    throw std::runtime_error("NO SUCH TABLE EXISTS");
}

parser_types::Predicate
parser::Parser::parse_where_clause(parser::token_iterator &tok_it) {
    parser_types::Predicate res;

    lexer_types::Token col = tok_it.get_next();
    lexer_types::Token op  = tok_it.get_next();
    lexer_types::Token val = tok_it.get_next();

    res.col   = col.token_value;
    res.op    = op.token_value;
    res.value = val.token_value;

    return res;
}

// revise this, for now works
parser_types::SELECT_AST
parser::Parser::parse_select_clause(parser::token_iterator &tok_it,
                                    schema::schema_manager &schema_manager,
                                    const std::string      &schema_name) {
    std::vector<std::string> raw_col_buffer;
    parser_types::SELECT_AST ast;

    while (tok_it.has_next()) {
        lexer_types::Token sub_tok = tok_it.get_next();

        if (sub_tok.token_value == "WHERE") {
            ast.predicate      = parse_where_clause(tok_it);
            ast.have_predicate = true;
            continue;
        } else {
            while (true) {
                if (sub_tok.token_value == "FROM") {
                    ast.table_name =
                        parse_from_clause(tok_it, schema_manager, schema_name);

                    for (const std::string &name : raw_col_buffer) {
                        if (name == "*") {
                            auto table_find = schema_manager.entity_find(
                                schema::TABLE, ast.table_name, schema_name);
                            if (table_find.has_value()) {
                                if (auto *tptr =
                                        std::get_if<schema::tables_attrs>(
                                            &table_find.value()[0])) {
                                    for (const auto &col : tptr->columns) {
                                        ast.cols_name.push_back(
                                            col.column_name);
                                    }
                                }
                            }
                        } else {
                            auto found = schema_manager.entity_find(
                                schema::COLUMN, name, ast.table_name,
                                schema_name);
                            if (found.has_value() && !found->empty()) {
                                for (const auto &ele : found.value()) {
                                    if (auto *ptr =
                                            std::get_if<schema::col_attrs>(
                                                &ele)) {
                                        ast.cols_name.push_back(
                                            ptr->column_name);
                                    }
                                }
                            } else {
                                throw std::runtime_error(
                                    "COLUMN '" + name +
                                    "' NOT FOUND IN TABLE " + ast.table_name);
                            }
                        }
                    }
                    raw_col_buffer.clear();

                } else if (sub_tok.token_type == lexer_types::IDENT ||
                           sub_tok.token_value == "*") {
                    raw_col_buffer.push_back(sub_tok.token_value);

                } else if (sub_tok.token_value == "," &&
                           sub_tok.token_type == lexer_types::OPERATOR) {
                    if (tok_it.peek(1).token_type != lexer_types::IDENT &&
                        tok_it.peek(1).token_value != "*") {
                        throw std::runtime_error(
                            "EXPECTED IDENTIFIER AFTER ','");
                    }
                }

                if (!tok_it.has_next() ||
                    tok_it.peek(1).token_value == "WHERE") {
                    break;
                }

                sub_tok = tok_it.get_next();
            }
        }
    }
    return ast;
}

void parser::Parser::parse_into_clause(parser::token_iterator   &tok_it,
                                       parser_types::INSERT_AST &ast,
                                       schema::schema_manager   &schema_manager,
                                       const std::string        &schema_name) {
    lexer_types::Token sub_tok = tok_it.get_next();
    // Support columns
    std::optional<std::vector<schema::ENTITY_TYPE>> table_match;
    if (sub_tok.token_type == lexer_types::IDENT) {
        table_match = schema_manager.entity_find(
            schema::TABLE, sub_tok.token_value, schema_name);

        if (table_match.has_value()) {
            ast.table_name = sub_tok.token_value;
        } else {
            throw std::runtime_error("NO SUCH TABLE FOUND");
        }
    } else {
        throw std::runtime_error("EXPECTED A TABLE NAME");
    }

    lexer_types::Token next_tok = tok_it.peek(1);

    if (next_tok.token_value == "(" &&
        next_tok.token_type == lexer_types::OPERATOR) {
        sub_tok = tok_it.get_next();
        while (sub_tok.token_value != ")") {
            std::optional<std::vector<schema::ENTITY_TYPE>> find =
                schema_manager.entity_find(schema::COLUMN, sub_tok.token_value,
                                           ast.table_name, schema_name);
            if (sub_tok.token_type == lexer_types::IDENT) {
                if (find.has_value()) {
                    ast.cols_name.push_back(sub_tok.token_value);
                } else {
                    throw std::runtime_error("NO SUCH COULMNS");
                }
            }
            sub_tok = tok_it.get_next();
        }
    } else {
        if (table_match.has_value()) {
            if (schema::tables_attrs *table_ptr =
                    std::get_if<schema::tables_attrs>(
                        &table_match.value()[0])) {
                std::vector<schema::col_attrs> schema_cols = table_ptr->columns;
                for (const schema::col_attrs &col : schema_cols) {
                    ast.cols_name.push_back(col.column_name);
                }
            }
        }
    }
}

void parser::Parser::parse_value_clause(parser::token_iterator   &tok_it,
                                        parser_types::INSERT_AST &ast,
                                        schema::schema_manager &schema_manager,
                                        const std::string      &schema_name) {
    lexer_types::Token sub_tok = tok_it.get_next();

    if (sub_tok.token_value == "(") {
        access_methods_types::row_t this_row;

        int                                             i = 0;
        std::optional<std::vector<schema::ENTITY_TYPE>> ast_table =
            schema_manager.entity_find(schema::TABLE, ast.table_name,
                                       schema_name);

        schema::tables_attrs *t_pt;
        if (ast_table.has_value()) {
            t_pt = std::get_if<schema::tables_attrs>(&ast_table.value()[0]);
            this_row.row.resize(t_pt->columns.size());
        }

        while (sub_tok.token_value != ")") {
            sub_tok = tok_it.get_next();
            if (sub_tok.token_type == lexer_types::IDENT) {

                std::optional<std::vector<schema::ENTITY_TYPE>> ast_col =
                    schema_manager.entity_find(schema::COLUMN, ast.cols_name[i],
                                               ast.table_name, schema_name);

                // some optimization look into that
                if (ast_col.has_value()) {

                    if (auto *pt = std::get_if<schema::col_attrs>(
                            &ast_col.value()[0])) {

                        if (pt->col_type_match(pt->column_type,
                                               sub_tok.token_value)) {

                            if (auto *t_pt = std::get_if<schema::tables_attrs>(
                                    &ast_table.value()[0])) {
                                for (int k = 0; k < t_pt->columns.size(); k++) {
                                    if (pt->column_name ==
                                        t_pt->columns[k].column_name) {
                                        this_row.row[k] = convert_correct_type(
                                            sub_tok.token_value,
                                            pt->column_type);
                                    }
                                }
                            }
                        } else {
                            throw std::runtime_error(
                                "VALUES TYPE DOES NOT MATCH COLUMN ORDER "
                                "GIVEN");
                        }
                    }
                }
                i++;
            }
        }
        ast.values.push_back(this_row);
    } else if (tok_it.get_prev().token_value == "(") {
        throw std::runtime_error("EXPECTED ( AFTER VALUES");
    }

    if (tok_it.has_next())
        parse_value_clause(tok_it, ast, schema_manager, schema_name);
}

parser_types::INSERT_AST
parser::Parser::parse_insert_clause(parser::token_iterator &tok_it,
                                    schema::schema_manager &schema_manager,
                                    const std::string      &schema_name) {
    parser_types::INSERT_AST ast;
    while (tok_it.has_next()) {
        lexer_types::Token sub_tok = tok_it.get_next();

        if (sub_tok.token_value == "INTO" &&
            sub_tok.token_type == lexer_types::CLAUSE) {
            parse_into_clause(tok_it, ast, schema_manager, schema_name);
            continue;
        } else if (tok_it.get_prev().token_value == "INSERT") {
            throw std::runtime_error("EXPECTED INTO KEYWORD AFTER INSERT");
        }

        if (sub_tok.token_value == "VALUES" &&
            sub_tok.token_type == lexer_types::CLAUSE) {
            parse_value_clause(tok_it, ast, schema_manager, schema_name);
            continue;
        } else if (tok_it.get_prev().token_value == ast.table_name) {
            throw std::runtime_error("EXPECTED VALUES KEYWORD AFTER INTO");
        }
    }
    return ast;
}

parser_types::SCHEMA_AST
parser::Parser::parse_cschema_clause(parser::token_iterator &tok_it,
                                     schema::schema_manager &schema_manager) {
    parser_types::SCHEMA_AST ast;
    lexer_types::Token       sub_token = tok_it.get_next();

    if (sub_token.token_value != "SCHEMA") {
        throw std::runtime_error("EXPECTED KEYWORD SCHEMA");
    }

    lexer_types::Token schema_name = tok_it.get_next();
    ast.schmea_name                = schema_name.token_value;
    return ast;
}

parser_types::CREATE_TABLE_AST
parser::Parser::parse_ctable_clause(parser::token_iterator &tok_it,
                                    schema::schema_manager &schema_manager,
                                    const std::string      &schema_name) {

    parser_types::CREATE_TABLE_AST ast;

    lexer_types::Token sub_token = tok_it.get_next();
    if (sub_token.token_value != "TABLE") {
        throw std::runtime_error("EXPECTED KEYWORD TABLE");
    }

    lexer_types::Token table_name = tok_it.get_next();
    if (table_name.token_type != lexer_types::IDENT) {
        throw std::runtime_error("EXPECTED TABLE NAME");
    }
    ast.table_name = table_name.token_value;

    lexer_types::Token next_tok = tok_it.get_next();
    if (next_tok.token_value != "(") {
        throw std::runtime_error("EXPECTED '(' FOR COLUMNS");
    }

    int col  = 0;
    next_tok = tok_it.get_next();

    while (true) {
        ast.columns.resize(col + 1);

        if (next_tok.token_type != lexer_types::IDENT) {
            throw std::runtime_error("EXPECTED COLUMN NAME");
        }

        ast.columns[col].column_name = next_tok.token_value;

        next_tok = tok_it.get_next();

        if (next_tok.token_value == "STRING" ||
            next_tok.token_value == "string") {
            ast.columns[col].column_type = access_methods_types::STRING;
        } else if (next_tok.token_value == "INT" ||
                   next_tok.token_value == "int") {
            ast.columns[col].column_type = access_methods_types::INTEGER;
        } else if (next_tok.token_value == "FLOAT" ||
                   next_tok.token_value == "float") {
            ast.columns[col].column_type = access_methods_types::FLOATING;
        } else {
            throw std::runtime_error("INVALID COLUMN TYPE");
        }

        next_tok = tok_it.get_next();

        if (next_tok.token_value == ")") {
            break;
        }

        if (next_tok.token_value != ",") {
            throw std::runtime_error("EXPECTED ',' OR ')'");
        }

        next_tok = tok_it.get_next();
        col++;
    }

    return ast;
}

parser_types::ASTResult
parser::Parser::grammer_check(parser::token_iterator &tok_it,
                              schema::schema_manager &schema_manager,
                              const std::string      &schema_name) {

    lexer_types::Token first_tok = tok_it.get_next();

    if (first_tok.token_value == "SELECT") {
        return parse_select_clause(tok_it, schema_manager, schema_name);
    }
    if (first_tok.token_value == "INSERT") {
        return parse_insert_clause(tok_it, schema_manager, schema_name);
    }
    if (first_tok.token_value == "CREATE") {
        lexer_types::Token next_tok = tok_it.peek(1);
        if (next_tok.token_value == "SCHEMA") {
            return parse_cschema_clause(tok_it, schema_manager);
        } else if (next_tok.token_value == "TABLE") {
            return parse_ctable_clause(tok_it, schema_manager, schema_name);
        }
    }

    throw std::runtime_error("COULD NOT CHECK GRAMMER");
}
