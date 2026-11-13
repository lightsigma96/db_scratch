#include "headers/heap_writer.hpp"
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <type_traits>
#include <variant>

heap_page_types::Slot heap_writer::heap_write(char *raw_heap_page, const access_methods_types::row_t &row,
                                              const std::vector<size_t> &row_data_size_arr) {

    heap_page_types::HeapPage *heap_page = reinterpret_cast<heap_page_types::HeapPage *>(raw_heap_page);

    /* std::cout << "WRITING VALUES: \n";
    for (const auto &ele : row.row) {
        std::visit([](auto &&val) { std::cout << val; }, ele);
    }
    std::cout << "\n"; */

    if (heap_page->page_header.is_initialized == false) {
        std::cout << "INITIALIZED PAGE";
        heap_page->initialize();
    }
    // header fill
    if (heap_page->page_header.slot_count < heap_page_types::MAX_SLOTS) {
        heap_page->page_header.slot_count += 1;
    } else {
        throw std::runtime_error("Slots Full");
    }

    size_t row_size = std::accumulate(row_data_size_arr.begin(), row_data_size_arr.end(), size_t(0));

    // Slot fill
    heap_page->slots[heap_page->page_header.slot_count - 1].slot_size = row_size;

    if (row_size <= heap_page->page_header.free_size) {
        heap_page->page_header.free_size -= row_size;
    } else {
        throw std::runtime_error("HEAP PAGE SPACE FULL");
    }

    heap_page->slots[heap_page->page_header.slot_count - 1].slot_offset = heap_page->page_header.free_size;

    heap_page->slots[heap_page->page_header.slot_count - 1].deleted = false;

    // Data fill
    size_t cum_offset = 0;
    char *start = heap_page->data + heap_page->slots[heap_page->page_header.slot_count - 1].slot_offset;
    for (int i = 0; i < row.row.size(); i++) {
        std::visit(
                [&](auto &&val) {
                    using T = std::decay_t<decltype(val)>;

                    if constexpr (std::is_same_v<T, int>) {
                        memcpy(start + cum_offset, &val, sizeof(int));
                        cum_offset += sizeof(int);
                    } else if constexpr (std::is_same_v<T, float>) {
                        memcpy(start + cum_offset, &val, sizeof(float));
                        cum_offset += sizeof(float);
                    } else if constexpr (std::is_same_v<T, std::string>) {
                        memcpy(start + cum_offset, val.c_str(), val.size());
                        memset(start + cum_offset + val.size(), '\0',
                               access_methods_types::STRING_MAX_SIZE - val.size()); // can also memset 0 whole file before writing
                        cum_offset += access_methods_types::STRING_MAX_SIZE;
                    }
                },
                row.row[i]);
    }

    return heap_page->slots[heap_page->page_header.slot_count - 1];
}

void heap_writer::delete_slot(buffer_manager::buffer_pool &buff_pool, heap_page_types::RID rid) {
    auto lock = std::make_unique<buffer_manager_types::BPLock>();
    lock->lock_type = buffer_manager_types::WRITE_LOCK;
    buff_pool.AcquireBufferPoolLock(lock);

    buffer_manager_types::Page *page = buff_pool.page_access(rid.pid, diskoperator_types::HEAP_PAGE);
    heap_page_types::HeapPage *heap_page_data = reinterpret_cast<heap_page_types::HeapPage *>(page->page_data);

    uint16_t offset = rid.slot.slot_offset;

    heap_page_types::Slot *del_slot = reinterpret_cast<heap_page_types::Slot *>(heap_page_data->data + offset);

    if (del_slot->deleted != true) {
        del_slot->deleted = true;
        page->dirty_bit = true;
    } else {
        buff_pool.un_pin(rid.pid, diskoperator_types::HEAP_PAGE);
        buff_pool.UnlockBufferPoolLock();
        throw std::runtime_error("Already Deleted(double delete)");
    }

    buff_pool.UnlockBufferPoolLock();
    buff_pool.un_pin(rid.pid, diskoperator_types::HEAP_PAGE);
}
