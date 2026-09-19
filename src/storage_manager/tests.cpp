#include "headers/buffer_manager.hpp"
#include "headers/types.hpp"
#include <cstddef>
#include <gtest/gtest.h>

class BufferManagerTest : public testing::Test {
  protected:
    buffer_manager::buffer_pool buffer_pool;
    BufferManagerTest()
        : buffer_pool(buffer_manager::buffer_pool("src/storage_manager/test_db_file", "src/storage_manager/test_index_file",
                                                  "src/storage_manager/test_wal_file")) {
    }

    void SetUp() override {
        for (int p = 0; p < buffer_manager_types::buffer_size; p++) {
            buffer_pool.page_access(p, diskoperator_types::HEAP_PAGE);
        }
    }

    void TearDown() override {
    }
};

// testing get_last_pid
TEST_F(BufferManagerTest, Get_Last_Pid) {
    size_t id = buffer_pool.get_last_pid(diskoperator_types::HEAP_PAGE);
    buffer_pool.page_access(id, diskoperator_types::HEAP_PAGE);
    EXPECT_EQ(id, 0);
}

// page transition get_last_pid

// Buffer full
TEST_F(BufferManagerTest, Eviction) {
    // all pages are pinned
    EXPECT_THROW(
        {
            try {
                buffer_pool.page_access(50, diskoperator_types::HEAP_PAGE);
            } catch (const std::runtime_error &e) {
                EXPECT_STREQ(e.what(), "All pages are pinned");
                throw;
            }
        },
        std::runtime_error);

    // unpin all, access a page, expect 0th page
    for (size_t i = 0; i < buffer_manager_types::buffer_size; i++) {
        buffer_pool.un_pin(i, diskoperator_types::HEAP_PAGE);
    }

    size_t id = buffer_pool.page_replacement_policy(diskoperator_types::HEAP_PAGE);

    EXPECT_EQ(id, 0);
}
