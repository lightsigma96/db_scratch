#pragma once

#include "../storage_manager/headers/types.hpp"
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace transaction_manager {

class LockManager {
  private:
    std::unordered_map<heap_page_types::RID, uint8_t, heap_page_types::RID_Hash> lock_table;
    std::unordered_map<uint8_t, heap_page_types::RID> lock_table_reverse;
    std::mutex lock_table_mutex;
    std::condition_variable lock_table_condvar;

    uint8_t get_transaction_id() {
        return (0x1B ^ 2) >> 2;
    }

  public:
    uint8_t AcquireLockFromLockTable(std::optional<heap_page_types::RID> rid);
    void ReleaseLockFromLockTable(const uint8_t &transaction_id);
};

} // namespace transaction_manager
