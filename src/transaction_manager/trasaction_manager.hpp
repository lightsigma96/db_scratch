#pragma once

#include "../wal/wal.hpp"
#include "lock_manager.hpp"
#include "worker_function.hpp"
#include <memory>
#include <thread>
#include <vector>

namespace transaction_manager {

class TransactionManager {
  private:
    uint8_t                                                       thread_id_seed;
    std::vector<std::unique_ptr<struct worker_functions::Worker>> Worker_Table;
    std::mutex Worker_Table_Mutex;
    buffer_manager::buffer_pool                                  &buff_pool;
    access_methods::Access_methods                               &access_methods;
    schema::schema_manager                                       &sch_ma;
    parser::Parser                                               &parser;
    LockManager                                                   lock_manager;
    WAL::WAL                                                      wal;

    uint8_t get_thread_id() {
        thread_id_seed++;
        return (0x1F ^ thread_id_seed) >> 1;
    }

  public:
    TransactionManager(schema::schema_manager &sch_ma, parser::Parser &parser, buffer_manager::buffer_pool &buff_pool,
                       access_methods::Access_methods &access_methods)
        : thread_id_seed(0), buff_pool(buff_pool), access_methods(access_methods), sch_ma(sch_ma), parser(parser), lock_manager(),
          wal(buff_pool) {
    }

    void IterateOrAddWorker(worker_functions::client &c);

    ~TransactionManager() {
        {
            std::lock_guard<std::mutex> table_lock(Worker_Table_Mutex);
            for (auto &worker : Worker_Table)
                worker->request_shutdown();
        }
        for (auto &worker : Worker_Table) {
            if (worker->thread.joinable())
                worker->thread.join();
        }
    }
};

} // namespace transaction_manager
