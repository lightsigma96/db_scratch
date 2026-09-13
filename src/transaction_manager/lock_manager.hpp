#pragma once

#include "../storage_manager/headers/types.hpp"
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace transaction_manager {

class LockManager {
  private:
    uint8_t                                                                                          transaction_id_seed;
    std::unordered_map<heap_page_types::RID, uint8_t, heap_page_types::RID_Hash>                     lock_table_reverse;
    std::unordered_map<uint8_t, std::unordered_set<heap_page_types::RID, heap_page_types::RID_Hash>> lock_table;
    std::mutex                                                                                       lock_table_mutex;
    std::condition_variable                                                                          lock_table_condvar;

  public:
    uint8_t get_transaction_id() {
        transaction_id_seed++;
        return (0x1B ^ transaction_id_seed) >> 2;
    }
    uint8_t AcquireLockFromLockTable(uint8_t &transaction_id, std::optional<heap_page_types::RID> rid);
    void    ReleaseLockFromLockTable(const uint8_t &transaction_id);
};

} // namespace transaction_manager
