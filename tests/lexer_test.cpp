#include "lexer.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using lexer_types::Token;

void expect_tokens(const std::vector<Token> &actual, const std::vector<Token> &expected) {
    ASSERT_EQ(actual.size(), expected.size());

    for (std::size_t i = 0; i < expected.size(); ++i) {
        SCOPED_TRACE("token index " + std::to_string(i));
        EXPECT_EQ(actual[i].token_type, expected[i].token_type);
        EXPECT_EQ(actual[i].token_value, expected[i].token_value);
    }
}

} // namespace

TEST(LexerTest, TokenizesSelectWithComparisonAndTerminator) {
    const auto tokens = lexer::lexer("SELECT name, age FROM students WHERE age >= 18;");

    const std::vector<Token> expected = {
        {lexer_types::CLAUSE, "SELECT"},
        {lexer_types::IDENT, "name"},
        {lexer_types::OPERATOR, ","},
        {lexer_types::IDENT, "age"},
        {lexer_types::CLAUSE, "FROM"},
        {lexer_types::IDENT, "students"},
        {lexer_types::CLAUSE, "WHERE"},
        {lexer_types::IDENT, "age"},
        {lexer_types::OPERATOR, ">="},
        {lexer_types::IDENT, "18"},
        {lexer_types::OPERATOR, ";"},
    };

    expect_tokens(tokens, expected);
}

TEST(LexerTest, RecognizesAdjacentMultiCharacterOperators) {
    const auto tokens = lexer::lexer("age==18 score<=100");

    const std::vector<Token> expected = {
        {lexer_types::IDENT, "age"},
        {lexer_types::OPERATOR, "=="},
        {lexer_types::IDENT, "18"},
        {lexer_types::IDENT, "score"},
        {lexer_types::OPERATOR, "<="},
        {lexer_types::IDENT, "100"},
    };

    expect_tokens(tokens, expected);
}

TEST(LexerTest, TokenizesInsertWithQuotedValues) {
    const auto tokens = lexer::lexer("INSERT INTO students (name, grade) VALUES ('Alice', 'A')");

    const std::vector<Token> expected = {
        {lexer_types::CLAUSE, "INSERT"},
        {lexer_types::CLAUSE, "INTO"},
        {lexer_types::IDENT, "students"},
        {lexer_types::OPERATOR, "("},
        {lexer_types::IDENT, "name"},
        {lexer_types::OPERATOR, ","},
        {lexer_types::IDENT, "grade"},
        {lexer_types::OPERATOR, ")"},
        {lexer_types::CLAUSE, "VALUES"},
        {lexer_types::OPERATOR, "("},
        {lexer_types::IDENT, "Alice"},
        {lexer_types::OPERATOR, ","},
        {lexer_types::IDENT, "A"},
        {lexer_types::OPERATOR, ")"},
    };

    expect_tokens(tokens, expected);
}

TEST(LexerTest, EmptyInputProducesNoTokens) {
    EXPECT_TRUE(lexer::lexer("").empty());
    EXPECT_TRUE(lexer::lexer("   ").empty());
}
