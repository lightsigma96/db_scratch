#include "../src/storage_manager/headers/access_methods.hpp"
#include "../src/storage_manager/headers/insert.hpp"
#include "../src/wal/wal.hpp"
#include <gtest/gtest.h>
#include <vector>

class TransactionManagerTest : public testing::Test {
  protected:
    buffer_manager::buffer_pool    buffer_pool;
    access_methods::Access_methods access_methods;
    WAL::WAL                       wal;

    TransactionManagerTest()
        : buffer_pool(buffer_manager::buffer_pool("src/storage_manager/test_db_file", "src/storage_manager/test_index_file",
                                                  "src/storage_manager/test_wal_file")),
          access_methods(access_methods::Access_methods()), wal(buffer_pool, access_methods) {
    }

    void SetUp() override {
    }

    void TearDown() override {
    }
};

TEST_F(TransactionManagerTest, Check_LSN_After_Transaction) {
    access_methods_types::row_t r;
    r.row.push_back(1);
    r.row.push_back("name");
    std::vector<size_t> sizes;
    sizes.push_back(4);
    sizes.push_back(4);

    auto res = insert::create_entry(buffer_pool, access_methods, r, sizes, nullptr, false, wal);
    auto p   = *buffer_pool.page_access(res.rid.pid, diskoperator_types::HEAP_PAGE);
    auto hp  = reinterpret_cast<heap_page_types::HeapPage *>(&p.page_data);
    ASSERT_EQ(hp->page_header.lsn, wal.get_lsn());
}

TEST_F(TransactionManagerTest, Offset_Check) {
}

TEST_F(TransactionManagerTest, LSN_Check) {
}

TEST_F(TransactionManagerTest, WAL_Replay) {
}
