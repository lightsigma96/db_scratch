#include "../src/query_manager/headers/planner.hpp"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace {

class VectorSource final : public planner::Operator {
  public:
    explicit VectorSource(std::vector<access_methods_types::row_t> rows)
        : rows_(std::move(rows)) {}

    void init() override { next_row_ = 0; }

    access_methods_types::ScanResult next() override {
        if (next_row_ >= rows_.size()) {
            return {access_methods_types::EOP, std::nullopt, true};
        }
        return {access_methods_types::SUCCESS, rows_[next_row_++], true};
    }

    void close() override {}

  private:
    std::vector<access_methods_types::row_t> rows_;
    std::size_t next_row_ = 0;
};

access_methods_types::row_t make_row(int id, std::string name) {
    access_methods_types::row_t row;
    row.row.emplace_back(id);
    row.row.emplace_back(std::move(name));
    return row;
}

} // namespace

TEST(FilterTest, ReturnsOnlyRowsMatchingIntegerEqualityPredicate) {
    schema::tables_attrs table;
    table.table_name = "users";
    table.columns = {
        {"id", access_methods_types::INTEGER},
        {"name", access_methods_types::STRING},
    };

    VectorSource source({
        make_row(1, "Ada"),
        make_row(2, "Grace"),
        make_row(3, "Linus"),
    });
    parser_types::Predicate predicate{"id", "==", "2"};

    planner::Filter filter(table, source, true, predicate);
    filter.init();

    const auto match = filter.next();
    ASSERT_EQ(match.scan_status, access_methods_types::SUCCESS);
    ASSERT_TRUE(match.scan_result.has_value());
    ASSERT_EQ(match.scan_result->row.size(), 2u);
    EXPECT_EQ(std::get<int>(match.scan_result->row[0]), 2);
    EXPECT_EQ(std::get<std::string>(match.scan_result->row[1]), "Grace");

    const auto end = filter.next();
    EXPECT_EQ(end.scan_status, access_methods_types::EOPs);
}
