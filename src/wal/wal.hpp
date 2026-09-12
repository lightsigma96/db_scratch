#pragma once
#include "../storage_manager/headers/buffer_manager.hpp"
#include <cstdio>
#include <filesystem>

namespace WAL {

class WAL {
  private:
    buffer_manager::buffer_pool &buff_pool;

  public:
    WAL(buffer_manager::buffer_pool &buff_pool) : buff_pool(buff_pool) {
    }

    void CommitTransaction(const heap_page_types::RID &rid, const char *operation) {
        wal_types::WAL_entry wale;

        wale.rid = rid;
        snprintf(wale.msg, MAX_QUERY_SIZE_WAL, "%s", operation);

        buff_pool.dp_write_to_wal(wale);
    }
};
} // namespace WAL
