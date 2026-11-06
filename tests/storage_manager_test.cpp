#include "../src/storage_manager/headers/buffer_manager.hpp"
#include "../src/storage_manager/headers/types.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

class BufferManagerTest : public testing::Test {
  protected:
    std::filesystem::path temporary_directory;
    std::unique_ptr<buffer_manager::buffer_pool> buffer_pool;

    void SetUp() override {
        const auto suffix =
            std::chrono::steady_clock::now().time_since_epoch().count();
        temporary_directory =
            std::filesystem::temp_directory_path() /
            ("db_scratch_buffer_manager_" + std::to_string(suffix));
        std::filesystem::create_directories(temporary_directory);

        const auto heap =
            (temporary_directory / "heap.bin").string();
        const auto index =
            (temporary_directory / "index.bin").string();
        const auto wal =
            (temporary_directory / "wal.bin").string();
        buffer_pool =
            std::make_unique<buffer_manager::buffer_pool>(heap, index, wal);

        for (int p = 0; p < static_cast<int>(buffer_manager_types::buffer_size);
             ++p) {
            buffer_pool->page_access(p, diskoperator_types::HEAP_PAGE);
        }
    }

    void TearDown() override {
        buffer_pool.reset();
        std::filesystem::remove_all(temporary_directory);
    }
};

TEST_F(BufferManagerTest, Get_Last_Pid) {
    size_t id = buffer_pool->get_last_pid(diskoperator_types::HEAP_PAGE);
    buffer_pool->page_access(static_cast<int>(id), diskoperator_types::HEAP_PAGE);
    EXPECT_EQ(id, 0u);
}

TEST_F(BufferManagerTest, Eviction) {
    EXPECT_THROW(
        {
            try {
                buffer_pool->page_access(50, diskoperator_types::HEAP_PAGE);
            } catch (const std::runtime_error &e) {
                EXPECT_STREQ(e.what(), "All pages are pinned");
                throw;
            }
        },
        std::runtime_error);

    for (size_t i = 0; i < buffer_manager_types::buffer_size; i++) {
        buffer_pool->un_pin(static_cast<int>(i), diskoperator_types::HEAP_PAGE);
    }

    size_t id =
        buffer_pool->page_replacement_policy(diskoperator_types::HEAP_PAGE);

    EXPECT_EQ(id, 0u);
}
