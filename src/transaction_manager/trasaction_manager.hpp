#pragma once
#include "../../src/catalog_manager/headers/schmea_manager.hpp"
#include "../../src/query_manager/headers/parser.hpp"
#include "worker_function.hpp"
#include <cstdint>

/* Name transaction manager sounds related to only transaction but handles
 * entire concurrency, lock manager and result buffer */

namespace transaction_manager {

class TransactionManager {
  private:
    std::vector<struct worker_functions::Worker> Worker_Table;
    buffer_manager::buffer_pool                 &buff_pool;
    access_methods::Access_methods              &access_methods;
    schema::schema_manager                      &sch_ma;
    parser::Parser                              &parser;

    uint8_t get_id(worker_functions::client c) {
        return c.fd << 1; // can return negative, would spike to max
    }
    // lock table

  public:
    TransactionManager(schema::schema_manager &sch_ma, parser::Parser &parser,
                       buffer_manager::buffer_pool    &buff_pool,
                       access_methods::Access_methods &access_methods)
        : buff_pool(buff_pool), access_methods(access_methods), sch_ma(sch_ma),
          parser(parser) {
    }
    void IterateAndAddWorker(worker_functions::client &c);

    ~TransactionManager() {
        for (const struct worker_functions::Worker &worker : Worker_Table) {
            worker.thread.join();
        }
    }
};
} // namespace transaction_manager
