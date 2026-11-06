#include "../src/storage_manager/headers/buffer_manager.hpp"

#include <ctime>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;

class BufferPoolConcurrencyTest : public testing::Test {
  protected:
    std::filesystem::path                        temporary_directory;
    std::string                                  heap_filename;
    std::string                                  index_filename;
    std::string                                  wal_filename;
    std::unique_ptr<buffer_manager::buffer_pool> pool;

    int BufferContentionFunc() {
        pool->buffer_pool_lock.lock();
        auto page = pool->page_access(0, diskoperator_types::HEAP_PAGE);
        pool->buffer_pool_lock.unlock();
        return page->page_id;
    }

    void SetUp() override {
        const auto suffix   = std::chrono::steady_clock::now().time_since_epoch().count();
        temporary_directory = std::filesystem::temp_directory_path() / ("db_scratch_buffer_concurrency_" + std::to_string(suffix));
        std::filesystem::create_directories(temporary_directory);
        heap_filename  = (temporary_directory / "heap.bin").string();
        index_filename = (temporary_directory / "index.bin").string();
        wal_filename   = (temporary_directory / "wal.bin").string();
        pool           = std::make_unique<buffer_manager::buffer_pool>(heap_filename, index_filename, wal_filename);
    }

    void TearDown() override {
        pool.reset();
        std::filesystem::remove_all(temporary_directory);
    }
};
} // namespace

TEST_F(BufferPoolConcurrencyTest, SamePageIdInHeapAndIndexUsesDifferentFrames) {
    buffer_manager_types::Page *heap_page  = nullptr;
    buffer_manager_types::Page *index_page = nullptr;
    {
        std::lock_guard<std::mutex> guard(pool->buffer_pool_lock);
        heap_page  = pool->page_access(0, diskoperator_types::HEAP_PAGE);
        index_page = pool->page_access(0, diskoperator_types::INDEX_PAGE);

        ASSERT_NE(heap_page, index_page);
        heap_page->page_data[0]  = 'H';
        index_page->page_data[0] = 'I';
        pool->dp_write_page(heap_page, diskoperator_types::HEAP_PAGE);
        pool->dp_write_page(index_page, diskoperator_types::INDEX_PAGE);
        pool->un_pin(0, diskoperator_types::HEAP_PAGE);
        pool->un_pin(0, diskoperator_types::INDEX_PAGE);
    }

    char          heap_byte  = 0;
    char          index_byte = 0;
    std::ifstream heap_file(heap_filename, std::ios::binary);
    std::ifstream index_file(index_filename, std::ios::binary);
    heap_file.read(&heap_byte, 1);
    index_file.read(&index_byte, 1);
    EXPECT_EQ(heap_byte, 'H');
    EXPECT_EQ(index_byte, 'I');
}

TEST_F(BufferPoolConcurrencyTest, ConcurrentPinnedAccessToOnePageIsConsistent) {
    constexpr int            thread_count = 8;
    constexpr int            iterations   = 100;
    std::atomic<int>         successful_accesses{0};
    std::vector<std::thread> threads;

    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&] {
            for (int j = 0; j < iterations; ++j) {
                std::lock_guard<std::mutex> guard(pool->buffer_pool_lock);
                auto                       *page = pool->page_access(5, diskoperator_types::HEAP_PAGE);
                if (page && page->page_id == 5)
                    successful_accesses.fetch_add(1);
                pool->un_pin(5, diskoperator_types::HEAP_PAGE);
            }
        });
    }
    for (auto &thread : threads)
        thread.join();

    EXPECT_EQ(successful_accesses.load(), thread_count * iterations);
    std::lock_guard<std::mutex> guard(pool->buffer_pool_lock);
    auto                       *page = pool->page_access(5, diskoperator_types::HEAP_PAGE);
    EXPECT_EQ(page->pin_count, 1);
    pool->un_pin(5, diskoperator_types::HEAP_PAGE);
    EXPECT_EQ(page->pin_count, 0);
}

TEST_F(BufferPoolConcurrencyTest, SingleLockContention) {
    std::vector<std::thread> thread_pool;
    constexpr int            thread_count = 100;
    thread_pool.reserve(thread_count);

    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < thread_count; i++) {
        thread_pool.emplace_back([this]() { BufferContentionFunc(); });
    }

    for (auto &t : thread_pool) {
        t.join();
    }

    auto end      = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    std::cout << "Single Lock Contention: " << duration.count() << "us" << "\n";
}

TEST_F(BufferPoolConcurrencyTest, EvictionRemovesOldMappingAndReloadsPageFromDisk) {
    for (int pid = 0; pid < static_cast<int>(buffer_manager_types::buffer_size); ++pid) {
        auto *page = pool->page_access(pid, diskoperator_types::HEAP_PAGE);
        if (pid == 0) {
            page->page_data[0] = 'P';
            page->dirty_bit    = true;
        }
        pool->un_pin(pid, diskoperator_types::HEAP_PAGE);
    }

    auto *new_page = pool->page_access(static_cast<int>(buffer_manager_types::buffer_size), diskoperator_types::HEAP_PAGE);
    EXPECT_EQ(new_page->page_id, static_cast<int>(buffer_manager_types::buffer_size));
    pool->un_pin(new_page->page_id, diskoperator_types::HEAP_PAGE);

    auto *reloaded = pool->page_access(0, diskoperator_types::HEAP_PAGE);
    EXPECT_EQ(reloaded->page_id, 0);
    EXPECT_EQ(reloaded->page_data[0], 'P');
    pool->un_pin(0, diskoperator_types::HEAP_PAGE);
}

TEST_F(BufferPoolConcurrencyTest, PageAccessRejectsNegativePageIds) {
    EXPECT_THROW(pool->page_access(-1, diskoperator_types::HEAP_PAGE), std::runtime_error);
}

TEST_F(BufferPoolConcurrencyTest, ThrowsWhenEveryBufferFrameIsPinned) {
    for (int pid = 0; pid < static_cast<int>(buffer_manager_types::buffer_size); ++pid)
        pool->page_access(pid, diskoperator_types::HEAP_PAGE);

    EXPECT_THROW(pool->page_access(static_cast<int>(buffer_manager_types::buffer_size), diskoperator_types::HEAP_PAGE), std::runtime_error);
}
