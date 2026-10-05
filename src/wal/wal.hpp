#pragma once
#include "../storage_manager/headers/access_methods.hpp"
#include "../storage_manager/headers/buffer_manager.hpp"
#include <cstdint>
#include <vector>

namespace WAL {

class WAL {
  private:
    buffer_manager::buffer_pool    &buff_pool;
    access_methods::Access_methods &access_methods;
    std::uint64_t                   LSN;

    std::vector<char> serialize_row(const access_methods_types::row_t &row);
    void apply_redo(const heap_page_types::RID *rid, const char *row_bytes);

  public:
    WAL(buffer_manager::buffer_pool &buff_pool, access_methods::Access_methods &access_methods);

    void CommitTransaction(const heap_page_types::RID &rid, const access_methods_types::row_t &row);
    void ReplayTransaction();
};
} // namespace WAL
