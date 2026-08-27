#pragma once
#include "worker_function.hpp"
#include <cstdint>
#include <sys/poll.h>
#include <unordered_map>

/* Name transaction manager sounds related to only transaction but handles
 * entire concurrency, lock manager and result buffer */

namespace transaction_manager {

class TransactionManager {
  private:
    std::vector<std::unique_ptr<struct worker_functions::Worker>> Worker_Table;
    buffer_manager::buffer_pool                                  &buff_pool;
    access_methods::Access_methods            &access_methods;
    schema::schema_manager                    &sch_ma;
    parser::Parser                            &parser;
    struct worker_functions::polltable_struct &poll_table_struct;
    std::unordered_map<heap_page_types::RID, uint8_t, heap_page_types::RID_Hash>
        lock_table;

    uint8_t get_thread_id(worker_functions::client c) {
        return c.fd << 1;
    }

  public:
    TransactionManager(schema::schema_manager &sch_ma, parser::Parser &parser,
                       buffer_manager::buffer_pool    &buff_pool,
                       access_methods::Access_methods &access_methods,
                       struct worker_functions::polltable_struct &poll_table)
        : buff_pool(buff_pool), access_methods(access_methods), sch_ma(sch_ma),
          parser(parser), poll_table_struct(poll_table) {
    }

    void IterateOrAddWorker(worker_functions::client &c);

    bool AcquireLockFromLockTable(uint8_t &id, heap_page_types::RID &rid);
    void ReleaseLockFromLockTable();

    ~TransactionManager() {
        for (auto &worker : Worker_Table) {
            worker->thread.join();
        }
    }
};
} // namespace transaction_manager
