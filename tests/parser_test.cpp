#include "parser.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>

namespace {

constexpr const char *SCHEMA_NAME = "parser_test_schema";

class ParserTest : public testing::Test {
  protected:
    std::filesystem::path schema_path;
    std::unique_ptr<schema::schema_manager> catalog;
    parser::Parser parser_instance;

    void SetUp() override {
        const auto unique_id =
            std::chrono::steady_clock::now().time_since_epoch().count();

        schema_path = std::filesystem::temp_directory_path() /
                      ("db_scratch_parser_test_" + std::to_string(unique_id) + ".bin");
        std::filesystem::remove(schema_path);
        catalog = std::make_unique<schema::schema_manager>(schema_path);
    }

    void TearDown() override {
        catalog.reset();
        std::filesystem::remove(schema_path);
    }

    bool register_users_table() {
        parser::token_iterator schema_tokens("CREATE SCHEMA parser_test_schema");
        auto schema_result =
            parser_instance.grammer_check(schema_tokens, *catalog, "");

        auto *schema_ast = std::get_if<parser_types::SCHEMA_AST>(&schema_result);
        if (schema_ast == nullptr) {
            return false;
        }
        catalog->create_schema(*schema_ast);

        parser::token_iterator table_tokens(
            "CREATE TABLE users (id INT, name STRING, score FLOAT)");
        auto table_result =
            parser_instance.grammer_check(table_tokens, *catalog, SCHEMA_NAME);

        auto *table_ast =
            std::get_if<parser_types::CREATE_TABLE_AST>(&table_result);
        if (table_ast == nullptr) {
            return false;
        }

        catalog->schema_create_table(SCHEMA_NAME, *table_ast);
        return true;
    }
};

} // namespace

TEST_F(ParserTest, ParsesCreateSchemaStatement) {
    parser::token_iterator tokens("CREATE SCHEMA analytics");

    const auto result =
        parser_instance.grammer_check(tokens, *catalog, "");

    const auto *ast = std::get_if<parser_types::SCHEMA_AST>(&result);
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->schmea_name, "analytics");
}

TEST_F(ParserTest, ParsesCreateTableColumnsAndTypes) {
    parser::token_iterator tokens(
        "CREATE TABLE users (id INT, name STRING, score FLOAT)");

    const auto result =
        parser_instance.grammer_check(tokens, *catalog, SCHEMA_NAME);

    const auto *ast =
        std::get_if<parser_types::CREATE_TABLE_AST>(&result);
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->table_name, "users");
    ASSERT_EQ(ast->columns.size(), 3u);
    EXPECT_EQ(ast->columns[0].column_name, "id");
    EXPECT_EQ(ast->columns[0].column_type,
              access_methods_types::INTEGER);
    EXPECT_EQ(ast->columns[1].column_name, "name");
    EXPECT_EQ(ast->columns[1].column_type,
              access_methods_types::STRING);
    EXPECT_EQ(ast->columns[2].column_name, "score");
    EXPECT_EQ(ast->columns[2].column_type,
              access_methods_types::FLOATING);
}

TEST_F(ParserTest, ParsesSelectProjectionAndPredicate) {
    ASSERT_TRUE(register_users_table());

    parser::token_iterator tokens(
        "SELECT name, score FROM users WHERE id >= 18");
    const auto result =
        parser_instance.grammer_check(tokens, *catalog, SCHEMA_NAME);

    const auto *ast = std::get_if<parser_types::SELECT_AST>(&result);
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->table_name, "users");
    ASSERT_EQ(ast->cols_name.size(), 2u);
    EXPECT_EQ(ast->cols_name[0], "name");
    EXPECT_EQ(ast->cols_name[1], "score");
    ASSERT_TRUE(ast->have_predicate);
    EXPECT_EQ(ast->predicate.col, "id");
    EXPECT_EQ(ast->predicate.op, ">=");
    EXPECT_EQ(ast->predicate.value, "18");
}

TEST_F(ParserTest, RejectsUnknownTable) {
    ASSERT_TRUE(register_users_table());

    parser::token_iterator tokens("SELECT id FROM missing_table");
    EXPECT_THROW(
        parser_instance.grammer_check(tokens, *catalog, SCHEMA_NAME),
        std::runtime_error);
}

TEST_F(ParserTest, RejectsUnknownColumn) {
    ASSERT_TRUE(register_users_table());

    parser::token_iterator tokens("SELECT missing_column FROM users");
    EXPECT_THROW(
        parser_instance.grammer_check(tokens, *catalog, SCHEMA_NAME),
        std::runtime_error);
}
