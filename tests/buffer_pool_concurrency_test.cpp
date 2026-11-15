#include "../src/storage_manager/headers/buffer_manager.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include <iostream>
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
        auto lock       = std::make_unique<buffer_manager_types::BPLock>();
        lock->lock_type = buffer_manager_types::WRITE_LOCK;
        pool->AcquireBufferPoolLock(lock);
        auto *page = pool->page_access(0, diskoperator_types::HEAP_PAGE);
        pool->UnlockBufferPoolLock();
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

TEST_F(BufferPoolConcurrencyTest, ConcurrentPinnedAccessToOnePageIsConsistent) {
    constexpr int            thread_count = 8;
    constexpr int            iterations   = 10;
    std::atomic<int>         successful_accesses{0};
    std::vector<std::thread> threads;

    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&] {
            for (int j = 0; j < iterations; ++j) {
                auto lock       = std::make_unique<buffer_manager_types::BPLock>();
                lock->lock_type = buffer_manager_types::READ_LOCK;
                pool->AcquireBufferPoolLock(lock);
                auto *page = pool->page_access(5, diskoperator_types::HEAP_PAGE);
                if (page && page->page_id == 5)
                    successful_accesses.fetch_add(1);
                pool->un_pin(5, diskoperator_types::HEAP_PAGE);
            }
        });
    }
    for (auto &thread : threads)
        thread.join();

    EXPECT_EQ(successful_accesses.load(), thread_count * iterations);

    auto lock       = std::make_unique<buffer_manager_types::BPLock>();
    lock->lock_type = buffer_manager_types::WRITE_LOCK;
    pool->AcquireBufferPoolLock(lock);
    auto *page = pool->page_access(5, diskoperator_types::HEAP_PAGE);
    EXPECT_EQ(page->pin_count, 1);
    pool->un_pin(5, diskoperator_types::HEAP_PAGE);
    EXPECT_EQ(page->pin_count, 0);
    pool->UnlockBufferPoolLock();
}

/* A writer stays queued as the active writer until it explicitly unlocks. */
TEST_F(BufferPoolConcurrencyTest, WriterBlocksLaterReadersAndWriters) {
    auto first_writer       = std::make_unique<buffer_manager_types::BPLock>();
    first_writer->lock_type = buffer_manager_types::WRITE_LOCK;
    pool->AcquireBufferPoolLock(first_writer);

    std::atomic<bool> reader_entered{false};
    std::atomic<bool> second_writer_entered{false};

    auto reader_lock = std::make_unique<buffer_manager_types::BPLock>();
    reader_lock->lock_type = buffer_manager_types::READ_LOCK;
    std::thread reader([&] {
        pool->AcquireBufferPoolLock(reader_lock);
        reader_entered.store(true);
    });

    auto second_writer_lock = std::make_unique<buffer_manager_types::BPLock>();
    second_writer_lock->lock_type = buffer_manager_types::WRITE_LOCK;
    std::thread second_writer([&] {
        pool->AcquireBufferPoolLock(second_writer_lock);
        second_writer_entered.store(true);
        pool->UnlockBufferPoolLock();
    });

    std::this_thread::sleep_for(50ms);
    EXPECT_FALSE(reader_entered.load());
    EXPECT_FALSE(second_writer_entered.load());

    pool->UnlockBufferPoolLock();

    const auto deadline = std::chrono::steady_clock::now() + 1s;
    while ((!reader_entered.load() || !second_writer_entered.load()) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }

    EXPECT_TRUE(reader_entered.load());
    EXPECT_FALSE(second_writer_entered.load());

    reader.join();
    EXPECT_FALSE(second_writer_entered.load());

    // The writer is next after the reader batch has been released.
    // Readers in this design do not call UnlockBufferPoolLock().
    // The next writer is granted by the first writer's unlock after the batch.
    second_writer.join();
}

/* Readers can proceed concurrently when there is no active or queued writer. */
TEST_F(BufferPoolConcurrencyTest, ReadersBypassWhenNoWriterIsWaiting) {
    constexpr int reader_count = 8;
    std::atomic<int> entered{0};
    std::vector<std::thread> readers;

    for (int i = 0; i < reader_count; ++i) {
        readers.emplace_back([&] {
            auto lock       = std::make_unique<buffer_manager_types::BPLock>();
            lock->lock_type = buffer_manager_types::READ_LOCK;
            pool->AcquireBufferPoolLock(lock);
            entered.fetch_add(1);
        });
    }

    for (auto &reader : readers)
        reader.join();

    EXPECT_EQ(entered.load(), reader_count);
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

/*
 * Queue shape behind a held write: R, R, W, R.
 * ProcessBufferPollLock bunches consecutive reads then stops at the first write.
 * So after the holder unlocks: R0+R1 hold together, W waits, R2 stays behind W.
 */
TEST_F(BufferPoolConcurrencyTest, OrdersWritersAndBunchesConsecutiveReaders) {
    auto holder       = std::make_unique<buffer_manager_types::BPLock>();
    holder->lock_type = buffer_manager_types::WRITE_LOCK;
    pool->AcquireBufferPoolLock(holder);

    struct Waiter {
        buffer_manager_types::BPLock_Type             type;
        std::promise<void>                            about_to_acquire;
        std::atomic<bool>                             holding{false};
        std::promise<void>                            may_release;
        std::unique_ptr<buffer_manager_types::BPLock> lock;
        std::thread                                   thread;
    };

    const buffer_manager_types::BPLock_Type types[] = {
        buffer_manager_types::READ_LOCK,
        buffer_manager_types::READ_LOCK,
        buffer_manager_types::WRITE_LOCK,
        buffer_manager_types::READ_LOCK,
    };
    constexpr int waiter_count = 4;
    Waiter        waiters[waiter_count];

    for (int i = 0; i < waiter_count; ++i) {
        waiters[i].type            = types[i];
        waiters[i].lock            = std::make_unique<buffer_manager_types::BPLock>();
        waiters[i].lock->lock_type = types[i];
        waiters[i].thread          = std::thread([this, &waiters, i] {
            waiters[i].about_to_acquire.set_value();
            pool->AcquireBufferPoolLock(waiters[i].lock);
            waiters[i].holding.store(true);
            waiters[i].may_release.get_future().wait();
            pool->UnlockBufferPoolLock();
        });
        waiters[i].about_to_acquire.get_future().wait();
        /* Let Acquire enqueue and block behind the holder before the next waiter. */
        std::this_thread::sleep_for(20ms);
        EXPECT_FALSE(waiters[i].holding.load()) << "waiter " << i << " should still be queued";
    }

    pool->UnlockBufferPoolLock();

    auto wait_until_holding = [](Waiter &w, std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!w.holding.load()) {
            if (std::chrono::steady_clock::now() >= deadline)
                return false;
            std::this_thread::sleep_for(1ms);
        }
        return true;
    };

    /* Consecutive readers at the front are granted together. */
    ASSERT_TRUE(wait_until_holding(waiters[0], 1s));
    ASSERT_TRUE(wait_until_holding(waiters[1], 1s));
    std::this_thread::sleep_for(50ms);
    EXPECT_FALSE(waiters[2].holding.load()) << "writer must wait until bunched readers release";
    EXPECT_FALSE(waiters[3].holding.load()) << "reader behind writer must not skip ahead";

    waiters[0].may_release.set_value();
    waiters[1].may_release.set_value();

    ASSERT_TRUE(wait_until_holding(waiters[2], 1s));
    std::this_thread::sleep_for(50ms);
    EXPECT_FALSE(waiters[3].holding.load()) << "reader behind writer waits for that writer";

    waiters[2].may_release.set_value();
    ASSERT_TRUE(wait_until_holding(waiters[3], 1s));
    waiters[3].may_release.set_value();

    for (auto &w : waiters)
        w.thread.join();
}

/* A held reader blocks a later writer until that reader unlocks. */
TEST_F(BufferPoolConcurrencyTest, WriterWaitsBehindHeldReader) {
    auto reader       = std::make_unique<buffer_manager_types::BPLock>();
    reader->lock_type = buffer_manager_types::READ_LOCK;
    pool->AcquireBufferPoolLock(reader);

    std::promise<void> about_to_acquire;
    auto               about_to_acquire_fut = about_to_acquire.get_future();
    std::atomic<bool>  writer_holding{false};

    std::thread writer([this, &about_to_acquire, &writer_holding] {
        about_to_acquire.set_value();
        auto lock       = std::make_unique<buffer_manager_types::BPLock>();
        lock->lock_type = buffer_manager_types::WRITE_LOCK;
        pool->AcquireBufferPoolLock(lock);
        writer_holding.store(true);
        pool->UnlockBufferPoolLock();
    });

    about_to_acquire_fut.wait();
    std::this_thread::sleep_for(50ms);
    EXPECT_FALSE(writer_holding.load());

    pool->UnlockBufferPoolLock();

    const auto deadline = std::chrono::steady_clock::now() + 1s;
    while (!writer_holding.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(1ms);
    EXPECT_TRUE(writer_holding.load());
    writer.join();
}
