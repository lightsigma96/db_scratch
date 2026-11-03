#pragma once
#include "../storage_manager/headers/buffer_manager.hpp"
#include <cstdint>
#include <mutex>
#include <vector>

namespace WAL {

class WAL {
  private:
    buffer_manager::buffer_pool    &buff_pool;
    std::uint64_t                   LSN;
    mutable std::mutex              wal_mutex;

    std::vector<char> serialize_row(const access_methods_types::row_t &row);
    void              apply_redo(const heap_page_types::RID *rid, const char *row_bytes, std::uint64_t record_lsn);

  public:
    explicit WAL(buffer_manager::buffer_pool &buff_pool);

    void     CommitTransaction(const heap_page_types::RID &rid, const access_methods_types::row_t &row);
    void     ReplayTransaction();
    uint64_t get_lsn() {
        std::lock_guard<std::mutex> guard(wal_mutex);
        return LSN;
    };
};
} // namespace WAL
