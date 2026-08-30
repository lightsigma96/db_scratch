#ifndef ACCESS_METHODS
#define ACCESS_METHODS

#include "buffer_manager.hpp"
#include "types.hpp"
#include <optional>

namespace transaction_manager {
class LockManager;
}

namespace access_methods {
struct split_res {
    int                       promoted_key;
    btree_page_types::node_id child_pid;
};

class Access_methods {
  private:
  public:
    explicit Access_methods();

    class heap_scan {
      private:
        std::vector<heap_page_types::page_id> HeapTable;
        int                          curr_pid  = 0;
        int                          curr_slot = 0;
        buffer_manager::buffer_pool &buff_pool;

      public:
        heap_scan(buffer_manager::buffer_pool &buff_pool) : HeapTable(), buff_pool(buff_pool) {
            HeapTable.push_back(curr_pid);
        };

        access_methods_types::ScanResult scan(
            const uint8_t &thread_id,
            std::vector<size_t> &data_size_arr,
            std::vector<access_methods_types::SUPORTED_COLUMN_TYPE> &col_types,
            transaction_manager::LockManager &lock_manager);

        void heap_table_push(heap_page_types::page_id pid);
    };

    std::optional<access_methods::split_res> bptree_leaf_insert(buffer_manager_types::Page *left_raw_page,
                                                                buffer_manager_types::Page *right_raw_page, int key,
                                                                heap_page_types::RID rid);

    std::optional<access_methods::split_res> bptree_internal_insert(buffer_manager_types::Page *left_raw_page,
                                                                    buffer_manager_types::Page *right_raw_page,
                                                                    btree_page_types::node_id child_pid, int key);

    access_methods::split_res bptree_internal_split(buffer_manager_types::Page *left_raw_page,
                                                    buffer_manager_types::Page *right_raw_page,
                                                    int *temp_keys, heap_page_types::page_id *temp_child_id);

    access_methods::split_res bptree_leaf_split(buffer_manager_types::Page *left_raw_page,
                                                buffer_manager_types::Page *right_raw_page,
                                                int *temp_keys, heap_page_types::RID *temp_rids);

    heap_page_types::RID bptree_scan(buffer_manager::buffer_pool &buff_pool,
                                     const heap_page_types::page_id curr_root_pid,
                                     const heap_page_types::page_id key);
};
} // namespace access_methods
#endif
