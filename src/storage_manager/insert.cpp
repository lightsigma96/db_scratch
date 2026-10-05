#include "headers/insert.hpp"
#include <iostream>
#include <optional>
#include <vector>

insert::transaction_result insert::create_entry(buffer_manager::buffer_pool &buff_pool, access_methods::Access_methods &access_methods,
                                                const access_methods_types::row_t &row, std::vector<size_t> row_data_sizes,
                                                index_write::root_struct *curr_root, bool use_index, WAL::WAL &wal) {

    uintmax_t last_heap_pid = buff_pool.get_last_pid(diskoperator_types::HEAP_PAGE);

    buff_pool.buffer_pool_lock.lock();
    buffer_manager_types::Page *raw_heap_page = buff_pool.page_access(last_heap_pid, diskoperator_types::HEAP_PAGE);
    buff_pool.buffer_pool_lock.unlock();

    heap_page_types::Slot res_slot = heap_writer::heap_write(raw_heap_page->page_data, row, row_data_sizes);
    raw_heap_page->dirty_bit       = true;
    heap_page_types::RID res_rid   = {raw_heap_page->page_id, res_slot};

    wal.CommitTransaction(res_rid, row);
    buff_pool.un_pin(last_heap_pid, diskoperator_types::HEAP_PAGE);

    if (use_index && curr_root != nullptr) {
        int key = 0;
        if (curr_root->root_pid == buffer_manager_types::INVALID_PAGE_ID) {
            buffer_manager_types::Page *index_root_page = index_write::fetch_page(buff_pool);
            curr_root->root_pid                         = index_root_page->page_id;
            buff_pool.un_pin(curr_root->root_pid, diskoperator_types::INDEX_PAGE);
        }
        index_write::index_insert(buff_pool, access_methods, curr_root->root_pid, key, res_rid, curr_root);
        return {curr_root->root_pid, true, res_rid};
    }
    return {std::nullopt, true, res_rid};
}
