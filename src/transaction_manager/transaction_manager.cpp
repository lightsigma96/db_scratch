#include "trasaction_manager.hpp"
#include <memory>
#include <thread>

/* Iterates to find the correct idle worker, if cannot find any then creates a
 * new worker. */
void transaction_manager::TransactionManager::IterateOrAddWorker(
    worker_functions::client &c) {

    for (auto &w : Worker_Table) {
        if (w->state == worker_functions::IDLE) {
            w->mut.lock();
            w->state  = worker_functions::BUSY;
            w->client = c;
            w->mut.unlock();
            return;
        }
    }

    auto w = std::make_unique<struct worker_functions::Worker>();

    w->state     = worker_functions::BUSY;
    w->client    = c;
    w->thread_id = get_id(c);

    auto *worker_ptr = w.get();

    Worker_Table.push_back(std::move(w));

    worker_ptr->thread =
        std::thread(worker_functions::Worker, std::ref(*worker_ptr),
                    std::ref(sch_ma), std::ref(parser), std::ref(buff_pool),
                    std::ref(access_methods), std::ref(poll_table_struct));
}
