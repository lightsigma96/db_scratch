#include "trasaction_manager.hpp"
#include <thread>

void transaction_manager::TransactionManager::IterateAndAddWorker(
    worker_functions::client &c) {

    for (struct worker_functions::Worker &w : Worker_Table) {
        if (w.state == worker_functions::IDLE) {
            w.state = worker_functions::BUSY;
        }
    }
    struct worker_functions::Worker w;
    std::thread t(worker_functions::Worker, w, std::ref(sch_ma),
                  std::ref(parser), std::ref(buff_pool),
                  std::ref(access_methods), std::ref(c.client_input));

    w = {get_id(c), c, worker_functions::BUSY, t};

    Worker_Table.push_back(w);
}
