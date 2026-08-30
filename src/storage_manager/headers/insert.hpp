#ifndef INSERT
#define INSERT

#include "../../../src/transaction_manager/lock_manager.hpp"
#include "heap_writer.hpp"
#include "index_writer.hpp"
#include "types.hpp"

namespace insert {
std::optional<heap_page_types::page_id> create_entry(buffer_manager::buffer_pool &buff_pool, access_methods::Access_methods &access_methods,
                                                     const access_methods_types::row_t &row, std::vector<size_t> row_data_sizes,
                                                     index_write::root_struct *curr_root, bool use_index,
                                                     transaction_manager::LockManager &lock_manager);
}

#endif
