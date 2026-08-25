#ifndef BUFFER_MANAGER
#define BUFFER_MANAGER

#include "disk_operator.hpp"
#include "types.hpp"
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <queue>
#include <stdexcept>
#include <stdio.h>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace buffer_manager {

// Key pages by (type,pid) to avoid heap/index PID collisions.
typedef std::uint64_t                                               PageKey;
typedef std::unordered_map<PageKey, buffer_manager_types::frame_id> Table_t;
typedef std::queue<buffer_manager_types::frame_id>                  Queue_t;
typedef std::vector<buffer_manager_types::Page>                     Frame_t;
typedef std::vector<heap_page_types::page_id>                       page_list;

class buffer_pool {
  private:
    Frame_t frames;

    Table_t table;

    Queue_t            replacement_check_queue;
    Disk_operator      disk_operator;
    const std::string &heap_filepath;
    const std::string &index_filepath;

    static PageKey make_key(heap_page_types::page_id      pid,
                            diskoperator_types::page_type type) {
        return (static_cast<PageKey>(type) << 32) |
               static_cast<std::uint32_t>(pid);
    }

  public:
    // MUST declare the constructor here if you define it in the cpp
    buffer_pool(const std::string &db_filename,
                const std::string &index_filename);

    buffer_manager_types::Page *page_access(heap_page_types::page_id      pid,
                                            diskoperator_types::page_type type);

    buffer_manager_types::frame_id
    page_replacement_policy(diskoperator_types::page_type type);

    void un_pin(heap_page_types::page_id      pid,
                diskoperator_types::page_type type);

    uintmax_t get_last_pid(diskoperator_types::page_type type);

    void dp_write_page(buffer_manager_types::Page   *page,
                       diskoperator_types::page_type type);

    void dp_read_page(buffer_manager_types::Page   *page,
                      diskoperator_types::page_type type);

    void final_write();

    ~buffer_pool() {
        std::string message = "\nHeap Page written\n";
        write(STDOUT_FILENO, message.data(), message.size());
        final_write();
    }
};
}; // namespace buffer_manager

#endif
