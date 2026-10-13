#include "../src/transaction_manager/lock_manager.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <future>

namespace {

using namespace std::chrono_literals;

heap_page_types::RID make_rid(heap_page_types::page_id page_id,
                              std::uint16_t slot_offset) {
    heap_page_types::Slot slot{};
    slot.slot_offset = slot_offset;
    return heap_page_types::RID{page_id, slot};
}

} // namespace

TEST(LockManagerTest, WaitsUntilTheCurrentOwnerReleasesTheSameRid) {
    transaction_manager::LockManager lock_manager;
    std::uint8_t owner_tid = 1;
    std::uint8_t waiter_tid = 2;
    const auto rid = make_rid(7, 64);

    EXPECT_EQ(lock_manager.AcquireLockFromLockTable(owner_tid, rid), owner_tid);

    std::promise<void> attempting_lock;
    auto attempting_lock_started = attempting_lock.get_future();
    auto waiter = std::async(std::launch::async, [&] {
        attempting_lock.set_value();
        return lock_manager.AcquireLockFromLockTable(waiter_tid, rid);
    });

    attempting_lock_started.wait();
    const auto status_before_release = waiter.wait_for(75ms);
    EXPECT_EQ(status_before_release, std::future_status::timeout);

    lock_manager.ReleaseLockFromLockTable(owner_tid);

    ASSERT_EQ(waiter.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(waiter.get(), waiter_tid);
    lock_manager.ReleaseLockFromLockTable(waiter_tid);
}

TEST(LockManagerTest, DifferentRidsCanBeLockedConcurrently) {
    transaction_manager::LockManager lock_manager;
    std::uint8_t first_tid = 10;
    std::uint8_t second_tid = 11;

    const auto first_rid = make_rid(2, 16);
    const auto second_rid = make_rid(2, 80);

    EXPECT_EQ(lock_manager.AcquireLockFromLockTable(first_tid, first_rid), first_tid);

    auto second_lock = std::async(std::launch::async, [&] {
        return lock_manager.AcquireLockFromLockTable(second_tid, second_rid);
    });

    const auto status_before_release = second_lock.wait_for(1s);
    if (status_before_release == std::future_status::ready) {
        EXPECT_EQ(second_lock.get(), second_tid);
        lock_manager.ReleaseLockFromLockTable(second_tid);
    }

    lock_manager.ReleaseLockFromLockTable(first_tid);
    EXPECT_EQ(status_before_release, std::future_status::ready);
}
