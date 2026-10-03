#pragma once
#include "../storage_manager/headers/access_methods.hpp"
#include "../storage_manager/headers/buffer_manager.hpp"
#include <cstdint>

namespace WAL {

class WAL {
  private:
    buffer_manager::buffer_pool    &buff_pool;
    access_methods::Access_methods &access_methods;
    uint16_t                        LSN;
    // given pid+slot offset (rid), change tuple state (row_t), should be independent of anything (no table schema or anything just raw row
    // written to disk)
    void apply_redo(const heap_page_types::RID *rid, const char *row_bytes);

  public:
    WAL(buffer_manager::buffer_pool &buff_pool, access_methods::Access_methods &access_methods)
        : buff_pool(buff_pool), access_methods(access_methods), LSN(0) {
    }

    void CommitTransaction(const heap_page_types::RID &rid, const char *operation);
    void ReplayTransaction();
};
} // namespace WAL
