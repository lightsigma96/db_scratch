#include "headers/access_methods.hpp"
#include "../transaction_manager/trasaction_manager.hpp"
#include "headers/types.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <variant>
#include <vector>

access_methods::Access_methods::Access_methods() {
}

access_methods_types::ScanResult
access_methods::Access_methods::heap_scan::scan(const uint8_t &thread_id, std::vector<size_t> &data_size_arr,
                                                std::vector<access_methods_types::SUPORTED_COLUMN_TYPE> &col_types,
                                                transaction_manager::LockManager                        &lock_manager) {
    while (true) {
        access_methods_types::row_t row;

        char                      *heap_page_data = buff_pool.page_access(curr_pid, diskoperator_types::HEAP_PAGE)->page_data;
        heap_page_types::HeapPage *heap_page      = reinterpret_cast<heap_page_types::HeapPage *>(heap_page_data);

        if (heap_page == NULL) {
            buff_pool.un_pin(curr_pid, diskoperator_types::HEAP_PAGE);
            return {access_methods_types::ScanStatus::ERR, std::nullopt}; // can also return EOPs
        }

        if (curr_slot == heap_page->page_header.slot_count) {
            buff_pool.un_pin(curr_pid, diskoperator_types::HEAP_PAGE);
            curr_pid++; // this should be equal to next pid for that particular
                        // table
            curr_slot = 0;
            return {access_methods_types::EOP, std::nullopt};
        }

        // reading individual row
        size_t cum_offset = heap_page->slots[curr_slot].slot_offset;
        if (!heap_page->slots[curr_slot].deleted) {
            if (data_size_arr.size() == col_types.size()) {
                for (int i = 0; i < data_size_arr.size(); i++) {
                    access_methods_types::VALUE_TYPE data;

                    if (col_types[i] == access_methods_types::STRING) {
                        data = std::string(heap_page->data + cum_offset);
                        cum_offset += access_methods_types::STRING_MAX_SIZE;
                    } else if (col_types[i] == access_methods_types::INTEGER) {
                        data = *reinterpret_cast<int *>(heap_page->data + cum_offset);
                        cum_offset += sizeof(int);
                    } else if (col_types[i] == access_methods_types::FLOATING) {
                        data = *reinterpret_cast<float *>(heap_page->data + cum_offset);
                        cum_offset += sizeof(float);
                    }

                    row.row.push_back(data);
                }
                curr_slot++;

            } else {
                throw std::runtime_error("COLUMNS SIZES AND TYPES ARRAYS SIZE MISMATCH");
            }
        } else {
            curr_slot++;
            continue;
        }

        buff_pool.un_pin(curr_pid, diskoperator_types::HEAP_PAGE);

        // check lock table, before returning row

        // slot size is not going to be used in lock table (for hashing),
        // currently skipping it wont cause much harm
        heap_page_types::Slot slot = {heap_page->slots[curr_slot].slot_size, heap_page->slots[curr_slot].slot_offset};
        heap_page_types::RID  rid  = {curr_pid, slot};
        uint8_t               tid  = lock_manager.AcquireLockFromLockTable(rid);

        return {access_methods_types::ScanStatus::SUCCESS, row, tid};
    }
}

void access_methods::Access_methods::heap_scan::heap_table_push(heap_page_types::page_id pid) {
    HeapTable.push_back(pid);
}

std::optional<access_methods::split_res> access_methods::Access_methods::bptree_leaf_insert(buffer_manager_types::Page *left_raw_page,
                                                                                            buffer_manager_types::Page *right_raw_page,
                                                                                            int key, heap_page_types::RID rid) {
    /* Pages can be NULL */

    btree_page_types::Node *index_page = reinterpret_cast<btree_page_types::Node *>(left_raw_page->page_data);
    if (index_page->key_count < btree_page_types::MAX_KEYS) {
        if (index_page->key_count == 0) {
            index_page->key_count                                        = 1;
            index_page->keys[index_page->key_count - 1]                  = key;
            index_page->data.leaf_node.values[index_page->key_count - 1] = rid;
            /* if (index_page->pid == buffer_manager_types::INVALID_PAGE_ID) {
                index_page->pid = left_pid;
            } */
        } else if (index_page->key_count != 0) {
            index_page->key_count += 1;

            int i = index_page->key_count - 2;
            while (i >= 0 && index_page->keys[i] > key && index_page->keys[i] != key) {
                index_page->keys[i + 1]                  = index_page->keys[i];
                index_page->data.leaf_node.values[i + 1] = index_page->data.leaf_node.values[i];
                i--;
            }

            index_page->keys[i + 1]                  = key;
            index_page->data.leaf_node.values[i + 1] = rid;
        }

    } else if (index_page->key_count >= btree_page_types::MAX_KEYS) {
        int                  temp_keys[btree_page_types::MAX_KEYS + 1];
        heap_page_types::RID temp_rids[btree_page_types::MAX_KEYS + 1];

        memcpy(temp_keys, index_page->keys, sizeof(int) * (index_page->key_count));
        memcpy(temp_rids, index_page->data.leaf_node.values, sizeof(heap_page_types::RID) * (index_page->key_count));

        int i = index_page->key_count - 1;
        while (i >= 0 && temp_keys[i] > key) {
            temp_keys[i + 1] = temp_keys[i];
            temp_rids[i + 1] = temp_rids[i];
            i--;
        }

        temp_keys[i + 1] = key;
        temp_rids[i + 1] = rid;
        if (right_raw_page == NULL) {
            throw std::runtime_error("RIGHT RAW PAGE RECIEVED NULL");
        }
        return bptree_leaf_split(left_raw_page, right_raw_page, temp_keys, temp_rids);
    }
    return std::nullopt;
}

std::optional<access_methods::split_res> access_methods::Access_methods::bptree_internal_insert(buffer_manager_types::Page *left_raw_page,
                                                                                                buffer_manager_types::Page *right_raw_page,
                                                                                                btree_page_types::node_id   child_pid,
                                                                                                int                         key) {

    btree_page_types::Node *internal_node = reinterpret_cast<btree_page_types::Node *>(left_raw_page->page_data);
    if (internal_node->key_count < btree_page_types::MAX_KEYS) {
        if (internal_node->key_count == 0) {
            internal_node->key_count += 1;
            internal_node->is_leaf                            = false;
            internal_node->keys[internal_node->key_count - 1] = key;
            /* if (internal_node->pid == buffer_manager_types::INVALID_PAGE_ID)
            { internal_node->pid = left_pid;
            } */
        } else if (internal_node->key_count != 0) {
            internal_node->key_count += 1;
            int i = internal_node->key_count - 2;

            while (i >= 0 && internal_node->keys[i] > key) {
                internal_node->keys[i + 1]                           = internal_node->keys[i];
                internal_node->data.internal_node.child_nodes[i + 2] = internal_node->data.internal_node.child_nodes[i + 1];
                i--;
            }

            internal_node->keys[i + 1]                           = key;
            internal_node->data.internal_node.child_nodes[i + 2] = child_pid;
        }
    } else if (internal_node->key_count >= btree_page_types::MAX_KEYS) {
        int                       temp_keys[btree_page_types::MAX_KEYS + 1];
        btree_page_types::node_id temp_child_id[btree_page_types::MAX_KEYS + 2];

        memcpy(temp_keys, internal_node->keys, sizeof(int) * (internal_node->key_count));
        memcpy(temp_child_id, internal_node->data.internal_node.child_nodes,
               sizeof(btree_page_types::node_id) * (internal_node->key_count + 1));

        int i = internal_node->key_count - 1;

        while (i >= 0 && temp_keys[i] > key) {
            temp_keys[i + 1]     = temp_keys[i];
            temp_child_id[i + 2] = temp_child_id[i + 1];
            i--;
        }
        temp_keys[i + 1]     = key;
        temp_child_id[i + 2] = child_pid;
        return bptree_internal_split(left_raw_page, right_raw_page, temp_keys, temp_child_id);
    }
    return std::nullopt;
};

access_methods::split_res access_methods::Access_methods::bptree_internal_split(buffer_manager_types::Page *left_raw_page,
                                                                                buffer_manager_types::Page *right_raw_page, int *temp_keys,
                                                                                heap_page_types::page_id *temp_child_id) {

    btree_page_types::Node *old_internal_node = reinterpret_cast<btree_page_types::Node *>(left_raw_page->page_data);
    btree_page_types::Node *new_internal_node = reinterpret_cast<btree_page_types::Node *>(right_raw_page->page_data);

    new_internal_node->is_leaf = false;

    memset(old_internal_node->keys, 0, sizeof(int) * btree_page_types::MAX_KEYS);
    memset(old_internal_node->data.internal_node.child_nodes, 0, sizeof(btree_page_types::node_id) * (btree_page_types::MAX_KEYS + 1));

    int total      = old_internal_node->key_count + 1;
    int split_idx  = (total) / 2;
    int left_size  = split_idx;
    int right_size = total - left_size - 1;

    memcpy(old_internal_node->keys, temp_keys, sizeof(int) * left_size);
    memcpy(new_internal_node->keys, temp_keys + left_size, sizeof(int) * right_size);
    memcpy(old_internal_node->data.internal_node.child_nodes, temp_child_id, sizeof(btree_page_types::node_id) * (left_size + 1));
    memcpy(new_internal_node->data.internal_node.child_nodes, temp_child_id + left_size + 1,
           sizeof(btree_page_types::node_id) * (right_size + 1));

    old_internal_node->key_count = left_size;
    new_internal_node->key_count = right_size;

    struct split_res res = {temp_keys[split_idx], right_raw_page->page_id};
    return res;
}

access_methods::split_res access_methods::Access_methods::bptree_leaf_split(buffer_manager_types::Page *left_raw_page,
                                                                            buffer_manager_types::Page *right_raw_page, int *temp_keys,
                                                                            heap_page_types::RID *temp_rids) {

    btree_page_types::Node *old_leaf_page = reinterpret_cast<btree_page_types::Node *>(left_raw_page->page_data);
    btree_page_types::Node *new_leaf_page = reinterpret_cast<btree_page_types::Node *>(right_raw_page->page_data);

    new_leaf_page->is_leaf = true;
    memset(old_leaf_page->keys, 0, sizeof(int) * btree_page_types::MAX_KEYS);
    memset(old_leaf_page->data.leaf_node.values, 0, sizeof(heap_page_types::RID) * btree_page_types::MAX_KEYS);

    int total      = old_leaf_page->key_count + 1;
    int split_idx  = (total + 1) / 2;
    int left_size  = split_idx;
    int right_size = total - left_size;

    memcpy(old_leaf_page->keys, temp_keys, sizeof(heap_page_types::page_id) * left_size);
    memcpy(new_leaf_page->keys, temp_keys + left_size, sizeof(heap_page_types::page_id) * right_size);
    memcpy(old_leaf_page->data.leaf_node.values, temp_rids, sizeof(heap_page_types::RID) * left_size);
    memcpy(new_leaf_page->data.leaf_node.values, temp_rids + left_size, sizeof(heap_page_types::RID) * right_size);

    new_leaf_page->data.leaf_node.next_leaf = old_leaf_page->data.leaf_node.next_leaf;
    old_leaf_page->data.leaf_node.next_leaf = right_raw_page->page_id;

    old_leaf_page->key_count = left_size;
    new_leaf_page->key_count = right_size;

    struct split_res res = {new_leaf_page->keys[0], right_raw_page->page_id};
    return res;
}

heap_page_types::RID access_methods::Access_methods::bptree_scan(buffer_manager::buffer_pool   &buff_pool,
                                                                 const heap_page_types::page_id curr_root_pid,
                                                                 const heap_page_types::page_id key) {

    buffer_manager_types::Page *curr_raw_page = buff_pool.page_access(curr_root_pid, diskoperator_types::INDEX_PAGE);
    btree_page_types::Node     *curr_page     = reinterpret_cast<btree_page_types::Node *>(curr_raw_page->page_data);

    while (!curr_page->is_leaf) {
        int i = 0;
        while (i < curr_page->key_count && key >= curr_page->keys[i]) {
            i++;
        }
        curr_raw_page = buff_pool.page_access(curr_page->data.internal_node.child_nodes[i], diskoperator_types::INDEX_PAGE);
        curr_page     = reinterpret_cast<btree_page_types::Node *>(curr_raw_page->page_data);
        buff_pool.un_pin(curr_raw_page->page_id, diskoperator_types::INDEX_PAGE);
    }
    if (curr_page->is_leaf) {
        for (int entry = 0; entry < curr_page->key_count; entry++) {
            if (curr_page->keys[entry] == key) {
                return curr_page->data.leaf_node.values[entry];
            }
        }
        throw std::runtime_error("NO SUCH ENTRY FOUND");
    } else {
        throw std::runtime_error("SOME ERROR OCCRUCED WHILE INDEX SCANNIG");
    }
}
