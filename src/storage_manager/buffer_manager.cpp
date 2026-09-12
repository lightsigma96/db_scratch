#include "headers/buffer_manager.hpp"
#include "headers/disk_operator.hpp"
#include <stdexcept>
#include <string>

buffer_manager::buffer_pool::buffer_pool(const std::string &db_filename, const std::string &index_filename, const std::string &wal_filename)
    : disk_operator(db_filename, index_filename, wal_filename, buffer_manager_types::page_data_size),
      frames(buffer_manager_types::buffer_size), table(buffer_manager_types::buffer_size), replacement_check_queue(),
      heap_filepath(db_filename), index_filepath(index_filename) {
    std::cout << "BUFFER CREATED";
}

buffer_manager_types::Page *buffer_manager::buffer_pool::page_access(heap_page_types::page_id pid, diskoperator_types::page_type type) {
    if (pid < 0) {
        throw std::runtime_error("NEGATIVE PID RECIEVED");
    }
    auto it = table.find(pid);

    if (it != table.end()) {
        frames[it->second].pin_count += 1;
        // std::cout << "\nPage Found\n";
        return &frames[it->second];
    } else {
        for (auto i = 0; i != frames.size(); ++i) {
            if (frames[i].page_id == buffer_manager_types::INVALID_PAGE_ID) {
                frames[i].page_id   = pid;
                frames[i].pin_count = 1;
                frames[i].type      = type;
                table[pid]          = i;
                replacement_check_queue.push(i);
                disk_operator.read_page(pid, frames[i].page_data, type);
                // heap_page_types::HeapPage *hp = reinterpret_cast<heap_page_types::HeapPage *>(frames[i].page_data);
                std::cout << "\nNew Page Created\n";
                return &frames[i];
            }
        }
    }
    buffer_manager_types::frame_id free_frame_id = page_replacement_policy(type);
    frames[free_frame_id].page_id                = pid;
    frames[free_frame_id].pin_count              = 1;
    frames[free_frame_id].type                   = type;
    frames[free_frame_id].dirty_bit              = false;

    table[pid] = free_frame_id;

    disk_operator.read_page(pid, frames[free_frame_id].page_data, type);

    return &frames[free_frame_id];
};

void buffer_manager::buffer_pool::un_pin(heap_page_types::page_id pid, diskoperator_types::page_type type) {
    auto                           page = table.find(pid);
    buffer_manager_types::frame_id frame_idx;

    if (page != table.end()) {
        frame_idx = page->second;
    } else {
        throw std::runtime_error("page not found");
    }
    if (frame_idx >= buffer_manager_types::buffer_size) {
        std::cout << "fault";
    }
    if (frames[frame_idx].pin_count > 0) {
        frames[frame_idx].pin_count -= 1;
    }
};
buffer_manager_types::frame_id buffer_manager::buffer_pool::page_replacement_policy(diskoperator_types::page_type type) {
    int idx;
    for (idx = 0; idx < replacement_check_queue.size(); idx++) {
        buffer_manager_types::frame_id id = replacement_check_queue.front();
        replacement_check_queue.pop();
        if (frames[id].pin_count == 0) {
            auto it = table.find(frames[id].page_id);

            if (it != table.end()) {
                if (frames[id].dirty_bit) {
                    disk_operator.write_page(it->first, frames[id].page_data, frames[id].type);
                }
                table.erase(it->first);
            }
            frames[id].page_id   = buffer_manager_types::INVALID_PAGE_ID;
            frames[id].dirty_bit = false;
            frames[id].pin_count = 0;
            return id;
        } else if (frames[id].pin_count > 0) {
            replacement_check_queue.push(id);
            continue;
        } else {
            throw std::runtime_error("Page replacement error, pin count invalid");
        }
    }
    throw std::runtime_error("All pages are pinned");
};

uintmax_t buffer_manager::buffer_pool::get_last_pid(diskoperator_types::page_type type) {
    uintmax_t lpid = disk_operator.last_pid(type);
    if (type == diskoperator_types::HEAP_PAGE) {
        if (lpid == 0) {
            return lpid;
        }
        heap_page_types::HeapPage *prev_page =
            reinterpret_cast<heap_page_types::HeapPage *>(page_access(lpid - 1, diskoperator_types::HEAP_PAGE)->page_data);

        un_pin(lpid - 1, type);
        if (prev_page->page_header.free_size > 0) {
            return lpid - 1;
        }
    }
    return lpid;
}

void buffer_manager::buffer_pool::dp_write_page(buffer_manager_types::Page *page, diskoperator_types::page_type type) {

    disk_operator.write_page(page->page_id, page->page_data, type);
}
void buffer_manager::buffer_pool::dp_read_page(buffer_manager_types::Page *page, diskoperator_types::page_type type) {

    disk_operator.read_page(page->page_id, page->page_data, type);
}
void buffer_manager::buffer_pool::final_write() {

    for (auto frame = frames.begin(); frame != frames.end(); ++frame) {
        if (frame->page_id != buffer_manager_types::INVALID_PAGE_ID && frame->dirty_bit) {
            disk_operator.write_page(frame->page_id, frame->page_data, frame->type);
        }
    }
}
