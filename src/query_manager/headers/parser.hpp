#ifndef PARSER
#define PARSER

#include "../../catalog_manager/headers/schmea_manager.hpp"
#include "types.hpp"

namespace parser {

class token_iterator {
  private:
    std::vector<lexer_types::Token> tokens;
    int                             curr_idx = 0;

  public:
    token_iterator(const std::string &query);
    /* Increaments internal index maintained for tokens, and return a token */
    lexer_types::Token get_next();
    /* idx : How many positions to peek in tokens relative to curr_idx*/
    lexer_types::Token peek(int idx) const;
    /* Does not Decreament internal index maintained for tokens, and return a
     * token */
    lexer_types::Token get_prev();
    bool               has_next();
};

class Parser {
  private:
    std::string parse_from_clause(parser::token_iterator &tok_it,
                                  schema::schema_manager &schema_manager,
                                  const std::string      &schema_name);
    parser_types::Predicate parse_where_clause(parser::token_iterator &tok_it);
    void parse_into_clause(parser::token_iterator   &tok_it,
                           parser_types::INSERT_AST &ast,
                           schema::schema_manager   &schema_manager,
                           const std::string        &schema_name);
    void parse_value_clause(parser::token_iterator   &tok_it,
                            parser_types::INSERT_AST &ast,
                            schema::schema_manager   &schema_manager,
                            const std::string        &schema_name);
    parser_types::INSERT_AST
    parse_insert_clause(parser::token_iterator &tok_it,
                        schema::schema_manager &schema_manager,
                        const std::string      &schema_name);
    parser_types::SCHEMA_AST
    parse_cschema_clause(parser::token_iterator &tok_it,
                         schema::schema_manager &schema_manager);
    parser_types::CREATE_TABLE_AST
    parse_ctable_clause(parser::token_iterator &tok_it,
                        schema::schema_manager &schema_manager,
                        const std::string      &schema_name);
    parser_types::SELECT_AST
    parse_select_clause(parser::token_iterator &tok_it,
                        schema::schema_manager &schema_manager,
                        const std::string      &schema_name);

  public:
    parser_types::ASTResult
    grammer_check(parser::token_iterator &tok_it,
                  schema::schema_manager &schema_manager,
                  const std::string      &schema_name);
};

} // namespace parser

#endif
