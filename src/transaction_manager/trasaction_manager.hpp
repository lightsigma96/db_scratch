#pragma once
#include "worker_function.hpp"
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <sys/poll.h>
#include <thread>
#include <unordered_map>

/* Name transaction manager sounds related to only transaction but handles
 * entire concurrency, lock manager and result buffer */

namespace transaction_manager {

class LockManager {
  private:
    std::unordered_map<heap_page_types::RID, uint8_t, heap_page_types::RID_Hash> lock_table;
    std::unordered_map<uint8_t, heap_page_types::RID>                            lock_table_reverse;
    std::mutex                                                                   lock_table_mutex;
    std::condition_variable                                                      lock_table_condvar;

    // make this dependent on thread id
    uint8_t get_transaction_id() {
        return (0x1B ^ 2) >> 2;
    }

  public:
    /* Blocks the calling thread if requested RID is occupied */
    uint8_t AcquireLockFromLockTable(std::optional<heap_page_types::RID> rid);
    void    ReleaseLockFromLockTable(const uint8_t &transaction_id);
};

class TransactionManager {
  private:
    /* Worker can only execute db_pipeline */
    std::vector<std::unique_ptr<struct worker_functions::Worker>> Worker_Table;
    buffer_manager::buffer_pool                                  &buff_pool;
    access_methods::Access_methods                               &access_methods;
    schema::schema_manager                                       &sch_ma;
    parser::Parser                                               &parser;
    struct worker_functions::polltable_struct                    &poll_table_struct;
    LockManager                                                   lock_manager;

    // make this dependent on thread id
    uint8_t get_thread_id() {
        return (0x1F ^ 2) >> 1;
    }

  public:
    TransactionManager(schema::schema_manager &sch_ma, parser::Parser &parser, buffer_manager::buffer_pool &buff_pool,
                       access_methods::Access_methods &access_methods, struct worker_functions::polltable_struct &poll_table)
        : buff_pool(buff_pool), access_methods(access_methods), sch_ma(sch_ma), parser(parser), poll_table_struct(poll_table),
          lock_manager(LockManager()) {
    }

    void IterateOrAddWorker(worker_functions::client &c);

    ~TransactionManager() {
        for (auto &worker : Worker_Table) {
            worker->thread.join();
        }
    }
};
} // namespace transaction_manager
