#include "../src/transaction_manager/lock_manager.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <unordered_set>
#include <vector>

namespace {
using namespace std::chrono_literals;

heap_page_types::RID make_rid(int page_id, std::uint16_t slot_offset) {
    heap_page_types::Slot slot{};
    slot.slot_offset = slot_offset;
    return {page_id, slot};
}
} // namespace

TEST(LockManagerTest, AcquiresAnUnlockedRidImmediately) {
    transaction_manager::LockManager locks;
    std::uint8_t tid = 1;
    EXPECT_EQ(locks.AcquireLockFromLockTable(tid, make_rid(1, 8)), tid);
}

TEST(LockManagerTest, WaitsUntilTheCurrentOwnerReleasesTheSameRid) {
    transaction_manager::LockManager locks;
    std::uint8_t owner = 1, waiter_tid = 2;
    const auto rid = make_rid(7, 64);
    locks.AcquireLockFromLockTable(owner, rid);

    std::promise<void> started;
    auto started_future = started.get_future();
    auto waiter = std::async(std::launch::async, [&] {
        started.set_value();
        return locks.AcquireLockFromLockTable(waiter_tid, rid);
    });
    started_future.wait();
    EXPECT_EQ(waiter.wait_for(75ms), std::future_status::timeout);

    locks.ReleaseLockFromLockTable(owner);
    ASSERT_EQ(waiter.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(waiter.get(), waiter_tid);
    locks.ReleaseLockFromLockTable(waiter_tid);
}

TEST(LockManagerTest, DifferentRidsCanBeLockedConcurrently) {
    transaction_manager::LockManager locks;
    std::uint8_t first = 10, second = 11;
    locks.AcquireLockFromLockTable(first, make_rid(2, 16));

    auto other = std::async(std::launch::async, [&] {
        return locks.AcquireLockFromLockTable(second, make_rid(2, 80));
    });
    ASSERT_EQ(other.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(other.get(), second);
    locks.ReleaseLockFromLockTable(first);
    locks.ReleaseLockFromLockTable(second);
}

TEST(LockManagerTest, DifferentPagesWithTheSameSlotOffsetAreIndependent) {
    transaction_manager::LockManager locks;
    std::uint8_t first = 1, second = 2;
    EXPECT_EQ(locks.AcquireLockFromLockTable(first, make_rid(1, 32)), first);
    EXPECT_EQ(locks.AcquireLockFromLockTable(second, make_rid(2, 32)), second);
    locks.ReleaseLockFromLockTable(first);
    locks.ReleaseLockFromLockTable(second);
}

TEST(LockManagerTest, SameTransactionCanReacquireItsOwnRid) {
    transaction_manager::LockManager locks;
    std::uint8_t owner = 5, waiter_tid = 6;
    const auto rid = make_rid(4, 24);
    locks.AcquireLockFromLockTable(owner, rid);
    EXPECT_EQ(locks.AcquireLockFromLockTable(owner, rid), owner);

    auto waiter = std::async(std::launch::async, [&] {
        return locks.AcquireLockFromLockTable(waiter_tid, rid);
    });
    EXPECT_EQ(waiter.wait_for(75ms), std::future_status::timeout);
    locks.ReleaseLockFromLockTable(owner);
    ASSERT_EQ(waiter.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(waiter.get(), waiter_tid);
    locks.ReleaseLockFromLockTable(waiter_tid);
}

TEST(LockManagerTest, ReleaseUnlocksEveryRidOwnedByTheTransaction) {
    transaction_manager::LockManager locks;
    std::uint8_t owner = 20, first_tid = 21, second_tid = 22;
    const auto first_rid = make_rid(3, 10), second_rid = make_rid(3, 20);
    locks.AcquireLockFromLockTable(owner, first_rid);
    locks.AcquireLockFromLockTable(owner, second_rid);

    auto first = std::async(std::launch::async, [&] {
        return locks.AcquireLockFromLockTable(first_tid, first_rid);
    });
    auto second = std::async(std::launch::async, [&] {
        return locks.AcquireLockFromLockTable(second_tid, second_rid);
    });
    EXPECT_EQ(first.wait_for(75ms), std::future_status::timeout);
    EXPECT_EQ(second.wait_for(75ms), std::future_status::timeout);

    locks.ReleaseLockFromLockTable(owner);
    ASSERT_EQ(first.wait_for(1s), std::future_status::ready);
    ASSERT_EQ(second.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(first.get(), first_tid);
    EXPECT_EQ(second.get(), second_tid);
    locks.ReleaseLockFromLockTable(first_tid);
    locks.ReleaseLockFromLockTable(second_tid);
}

TEST(LockManagerTest, ReleasingAnUnrelatedTransactionDoesNotReleaseOwnersLock) {
    transaction_manager::LockManager locks;
    std::uint8_t owner = 30, unrelated = 31, waiter_tid = 32;
    const auto rid = make_rid(8, 12);
    locks.AcquireLockFromLockTable(owner, rid);
    locks.ReleaseLockFromLockTable(unrelated);

    auto waiter = std::async(std::launch::async, [&] {
        return locks.AcquireLockFromLockTable(waiter_tid, rid);
    });
    EXPECT_EQ(waiter.wait_for(75ms), std::future_status::timeout);
    locks.ReleaseLockFromLockTable(owner);
    ASSERT_EQ(waiter.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(waiter.get(), waiter_tid);
    locks.ReleaseLockFromLockTable(waiter_tid);
}

TEST(LockManagerTest, ReleasingTheSameTransactionMoreThanOnceIsSafe) {
    transaction_manager::LockManager locks;
    std::uint8_t tid = 40;
    locks.AcquireLockFromLockTable(tid, make_rid(1, 1));
    EXPECT_NO_THROW(locks.ReleaseLockFromLockTable(tid));
    EXPECT_NO_THROW(locks.ReleaseLockFromLockTable(tid));

    std::uint8_t next_tid = 41;
    EXPECT_EQ(locks.AcquireLockFromLockTable(next_tid, make_rid(1, 1)), next_tid);
    locks.ReleaseLockFromLockTable(next_tid);
}

TEST(LockManagerTest, NullRidDoesNotAcquireOrBlockAnyRowLock) {
    transaction_manager::LockManager locks;
    std::uint8_t first = 50, second = 51;
    EXPECT_EQ(locks.AcquireLockFromLockTable(first, std::nullopt), first);
    EXPECT_EQ(locks.AcquireLockFromLockTable(second, make_rid(9, 9)), second);
    locks.ReleaseLockFromLockTable(first);
    locks.ReleaseLockFromLockTable(second);
}

TEST(LockManagerTest, MultipleWaitersAcquireTheSameRidOneAtATime) {
    transaction_manager::LockManager locks;
    std::uint8_t owner = 60, first_tid = 61, second_tid = 62;
    const auto rid = make_rid(10, 16);
    locks.AcquireLockFromLockTable(owner, rid);

    auto first = std::async(std::launch::async, [&] {
        return locks.AcquireLockFromLockTable(first_tid, rid);
    });
    auto second = std::async(std::launch::async, [&] {
        return locks.AcquireLockFromLockTable(second_tid, rid);
    });
    EXPECT_EQ(first.wait_for(75ms), std::future_status::timeout);
    EXPECT_EQ(second.wait_for(75ms), std::future_status::timeout);

    locks.ReleaseLockFromLockTable(owner);
    const auto first_status = first.wait_for(1s);
    const auto second_status = second.wait_for(1s);
    ASSERT_TRUE(first_status == std::future_status::ready ||
                second_status == std::future_status::ready);

    if (first_status == std::future_status::ready) {
        EXPECT_EQ(first.get(), first_tid);
        EXPECT_EQ(second.wait_for(75ms), std::future_status::timeout);
        locks.ReleaseLockFromLockTable(first_tid);
        ASSERT_EQ(second.wait_for(1s), std::future_status::ready);
        EXPECT_EQ(second.get(), second_tid);
    } else {
        EXPECT_EQ(second.get(), second_tid);
        EXPECT_EQ(first.wait_for(75ms), std::future_status::timeout);
        locks.ReleaseLockFromLockTable(second_tid);
        ASSERT_EQ(first.wait_for(1s), std::future_status::ready);
        EXPECT_EQ(first.get(), first_tid);
    }
    locks.ReleaseLockFromLockTable(first_tid);
    locks.ReleaseLockFromLockTable(second_tid);
}

TEST(LockManagerTest, ConcurrentTransactionIdRequestsAreUnique) {
    transaction_manager::LockManager locks;
    std::vector<std::future<std::uint8_t>> requests;
    for (int i = 0; i < 128; ++i) {
        requests.emplace_back(std::async(std::launch::async, [&] {
            return locks.get_transaction_id();
        }));
    }
    std::unordered_set<std::uint8_t> ids;
    for (auto &request : requests)
        ids.insert(request.get());
    EXPECT_EQ(ids.size(), 128u);
}

TEST(LockManagerTest, ManyIndependentRidsCanBeAcquiredInParallel) {
    transaction_manager::LockManager locks;
    std::vector<std::future<std::uint8_t>> requests;
    for (int i = 0; i < 32; ++i) {
        requests.emplace_back(std::async(std::launch::async, [&, i] {
            std::uint8_t tid = static_cast<std::uint8_t>(i + 1);
            locks.AcquireLockFromLockTable(
                tid, make_rid(i, static_cast<std::uint16_t>(i)));
            return tid;
        }));
    }
    for (auto &request : requests)
        locks.ReleaseLockFromLockTable(request.get());
}
