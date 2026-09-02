#pragma once

#include "lock_manager.hpp"
#include "worker_function.hpp"
#include <memory>
#include <thread>
#include <vector>

/* Name transaction manager sounds related to only transaction but handles
 * entire concurrency, lock manager and result buffer */

namespace transaction_manager {

class TransactionManager {
  private:
    std::vector<std::unique_ptr<struct worker_functions::Worker>> Worker_Table;
    buffer_manager::buffer_pool                                  &buff_pool;
    access_methods::Access_methods                               &access_methods;
    schema::schema_manager                                       &sch_ma;
    parser::Parser                                               &parser;
    LockManager                                                   lock_manager;

    uint8_t get_thread_id() {
        return (0x1F ^ 2) >> 1;
    }

  public:
    TransactionManager(schema::schema_manager &sch_ma, parser::Parser &parser, buffer_manager::buffer_pool &buff_pool,
                       access_methods::Access_methods &access_methods)
        : buff_pool(buff_pool), access_methods(access_methods), sch_ma(sch_ma), parser(parser), lock_manager() {
    }

    void IterateOrAddWorker(worker_functions::client &c);

    ~TransactionManager() {
        for (auto &worker : Worker_Table) {
            if (worker->thread.joinable())
                worker->thread.join();
        }
    }
};

} // namespace transaction_manager
