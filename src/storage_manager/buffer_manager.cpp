#include "headers/buffer_manager.hpp"
#include "headers/disk_operator.hpp"
#include "headers/types.hpp"
#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <unistd.h>

/*
 * This function allows you to enter the critical section if no writer is waiting, if there is even 1, then you are buffered and should
 * wait. Note : Currently assuming that concurrent writes are being done on same page.
 */
void buffer_manager::buffer_pool::AcquireBufferPoolLock(std::unique_ptr<buffer_manager_types::BPLock> &thread) {
    // Increament waiting writers
    if (thread->lock_type == buffer_manager_types::WRITE_LOCK) {
        writers_waiting.fetch_add(1);
    }

    /* if writers waiting, buffer all types of threads.
     * the lock used here is sacrifical_lock, meaning only for sake of cond_var.
     */
    lock_queue_mutex.lock();
    if (writers_waiting > 0) {
        buffer_pool_lock.push_back(thread.get());
        lock_queue_mutex.unlock();
        std::unique_lock<std::mutex> sacrifical_lock(thread->mut);
        thread->cond_var.wait(sacrifical_lock);
    }
}

/* Should be called by a writer, ignore readers, pops that writer and process reads after it until a writer is met at which point that
 * writer would be woken up and this exits */
void buffer_manager::buffer_pool::UnlockBufferPoolLock() {
    lock_queue_mutex.lock();

    buffer_pool_lock.pop_front();
    writers_waiting.fetch_sub(1);

    auto front = buffer_pool_lock.front();
    while (front->lock_type != buffer_manager_types::WRITE_LOCK) {
        front->cond_var.notify_one();
        buffer_pool_lock.pop_front();
        front = buffer_pool_lock.front();
    }
    // notify next writer
    front->cond_var.notify_one();
}

/* void buffer_manager::buffer_pool::ProcessBufferPollLock() {
    std::lock_guard<std::mutex> queue_lock(lock_queue_mutex);
    if (buffer_pool_lock.empty()) {
        return;
    }

    if (!buffer_pool_lock.front()->waiting) {
        return;
    }

    if (buffer_pool_lock.front()->lock_type == buffer_manager_types::WRITE_LOCK) {
        buffer_pool_lock.front()->waiting = false;
        buffer_pool_lock.front()->cond_var.notify_one();
        return;
    }

    for (auto *waiter : buffer_pool_lock) {
        if (waiter->lock_type != buffer_manager_types::READ_LOCK) {
            break;
        }
        if (waiter->waiting) {
            waiter->waiting = false;
            waiter->cond_var.notify_one();
        }
    }
} */

buffer_manager_types::Page *buffer_manager::buffer_pool::page_access(heap_page_types::page_id pid, diskoperator_types::page_type type) {
    if (pid < 0) {
        throw std::runtime_error("NEGATIVE PID RECIEVED");
    }
    const PageKey key = make_key(pid, type);
    auto          it  = table.find(key);

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
                table[key]          = i;
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

    table[key] = free_frame_id;
    replacement_check_queue.push(free_frame_id);

    disk_operator.read_page(pid, frames[free_frame_id].page_data, type);

    return &frames[free_frame_id];
};

void buffer_manager::buffer_pool::un_pin(heap_page_types::page_id pid, diskoperator_types::page_type type) {
    auto                           page = table.find(make_key(pid, type));
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
            const PageKey key = make_key(frames[id].page_id, frames[id].type);
            auto          it  = table.find(key);

            if (it != table.end()) {
                if (frames[id].dirty_bit)
                    disk_operator.write_page(frames[id].page_id, frames[id].page_data, frames[id].type);
                table.erase(it);
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

void buffer_manager::buffer_pool::dp_write_to_wal(const wal_types::WAL_entry &record, const char *row_bytes) {
    disk_operator.write_to_wal(record, row_bytes);
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
            frame->dirty_bit = false;
            table.erase(make_key(frame->page_id, frame->type));
            frame->pin_count = 0;
            frame->page_id   = buffer_manager_types::INVALID_PAGE_ID; // empty buffer pool
        }
    }
}
