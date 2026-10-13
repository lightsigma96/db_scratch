#include "lock_manager.hpp"

#include <mutex>

uint8_t transaction_manager::LockManager::AcquireLockFromLockTable(
    uint8_t &tid, std::optional<heap_page_types::RID> rid) {
    std::unique_lock<std::mutex> lock(lock_table_mutex);

    if (rid.has_value()) {
        while (lock_table_reverse.find(rid.value()) != lock_table_reverse.end()) {
            lock_table_condvar.wait(lock);
        }

        lock_table_reverse[rid.value()] = tid;
        lock_table[tid].insert(rid.value());
    }

    return tid;
}

void transaction_manager::LockManager::ReleaseLockFromLockTable(const uint8_t &tid) {
    std::unique_lock<std::mutex> lock(lock_table_mutex);

    auto if_present_tid = lock_table.find(tid);
    if (if_present_tid == lock_table.end()) {
        // Transactions with no row locks need no cleanup.
        return;
    }

    for (const auto &rid : if_present_tid->second) {
        lock_table_reverse.erase(rid);
    }
    lock_table.erase(if_present_tid);

    lock.unlock();
    lock_table_condvar.notify_all();
}
