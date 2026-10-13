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
            w->state  = worker_functions::BUSY;
            w->client = c;
            w->condvar.notify_one();
            return;
        }
    }

    auto w       = std::make_unique<struct worker_functions::Worker>();
    w->state     = worker_functions::BUSY;
    w->client    = c;
    w->thread_id = get_thread_id();

    auto *worker_ptr = w.get();
    Worker_Table.push_back(std::move(w));

    worker_ptr->thread = std::thread(worker_functions::Worker, std::ref(*worker_ptr), std::ref(sch_ma), std::ref(parser),
                                     std::ref(buff_pool), std::ref(access_methods), std::ref(lock_manager), std::ref(wal));
}

