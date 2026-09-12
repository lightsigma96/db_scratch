#pragma once
#include "../storage_manager/headers/buffer_manager.hpp"
#include <filesystem>

namespace WAL {

class WAL {
  private:
    buffer_manager::buffer_pool &buff_pool;

  public:
    WAL(buffer_manager::buffer_pool &buff_pool) : buff_pool(buff_pool) {
    }

    void CommitTransaction() {
        uintmax_t last_pid = buff_pool.get_last_pid(diskoperator_types::WAL_PAGE);
        reinterpret_cast<type>(buff_pool.page_access(last_pid, diskoperator_types::WAL_PAGE));
        buff_pool.dp_write_page(buffer_manager_types::Page * page, diskoperator_types::WAL_PAGE);
    }
};
} // namespace WAL
