#include "trasaction_manager.hpp"
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>

void transaction_manager::TransactionManager::IterateOrAddWorker(worker_functions::client &c) {
    for (auto &w : Worker_Table) {
        std::lock_guard<std::mutex> lock(w->mut);
        if (w->state == worker_functions::IDLE) {
            w->state = worker_functions::BUSY;
            w->client = c;
            w->condvar.notify_one();
            return;
        }
    }

    auto w = std::make_unique<worker_functions::Worker>();
    w->state = worker_functions::BUSY;
    w->client = c;
    w->thread_id = get_thread_id();

    auto *worker_ptr = w.get();
    Worker_Table.push_back(std::move(w));

    worker_ptr->thread = std::thread(worker_functions::Worker, std::ref(*worker_ptr), std::ref(sch_ma), std::ref(parser),
                                     std::ref(buff_pool), std::ref(access_methods), std::ref(poll_table_struct), std::ref(lock_manager));
}

uint8_t transaction_manager::LockManager::AcquireLockFromLockTable(std::optional<heap_page_types::RID> rid) {
    std::unique_lock<std::mutex> lock(lock_table_mutex);
    uint8_t tid = get_transaction_id();

    if (rid.has_value()) {
        while (lock_table.find(rid.value()) != lock_table.end()) {
            lock_table_condvar.wait(lock);
        }
        lock_table[rid.value()] = tid;
        lock_table_reverse[tid] = rid.value();
    }

    return tid;
}

void transaction_manager::LockManager::ReleaseLockFromLockTable(const uint8_t &tid) {
    std::unique_lock<std::mutex> lock(lock_table_mutex);

    auto if_present_tid = lock_table_reverse.find(tid);
    if (if_present_tid == lock_table_reverse.end()) {
        throw std::runtime_error("UNABLE TO LOCATE RID FOR GIVEN TRANSACTION ID");
    }

    lock_table.erase(if_present_tid->second);
    lock_table_reverse.erase(if_present_tid);

    lock.unlock();
    lock_table_condvar.notify_all();
}
