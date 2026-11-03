#include "lock_manager.hpp"

#include <mutex>

uint8_t transaction_manager::LockManager::AcquireLockFromLockTable(
    uint8_t &tid, std::optional<heap_page_types::RID> rid) {
    if (!rid.has_value())
        return tid;

    std::unique_lock<std::mutex> lock(lock_table_mutex);
    auto owner = lock_table_reverse.find(rid.value());

    while (owner != lock_table_reverse.end() && owner->second != tid) {
        lock_table_condvar.wait(lock);
        owner = lock_table_reverse.find(rid.value());
    }

    // Reacquiring a lock already owned by this transaction is idempotent.
    if (owner == lock_table_reverse.end()) {
        lock_table_reverse.emplace(rid.value(), tid);
        lock_table[tid].insert(rid.value());
    }

    return tid;
}

void transaction_manager::LockManager::ReleaseLockFromLockTable(
    const uint8_t &tid) {
    std::unique_lock<std::mutex> lock(lock_table_mutex);

    auto owned_locks = lock_table.find(tid);
    if (owned_locks == lock_table.end())
        return;

    for (const auto &rid : owned_locks->second) {
        auto owner = lock_table_reverse.find(rid);
        if (owner != lock_table_reverse.end() && owner->second == tid)
            lock_table_reverse.erase(owner);
    }
    lock_table.erase(owned_locks);

    lock.unlock();
    lock_table_condvar.notify_all();
}
