#ifndef BUFFER_MANAGER
#define BUFFER_MANAGER

#include "disk_operator.hpp"
#include "types.hpp"
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <queue>
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

    Queue_t                                                                            replacement_check_queue;
    Disk_operator                                                                      disk_operator;
    const std::string                                                                 &heap_filepath;
    const std::string                                                                 &index_filepath;
    std::vector<std::reference_wrapper<std::unique_ptr<buffer_manager_types::BPLock>>> buffer_pool_lock;

    static PageKey make_key(heap_page_types::page_id pid, diskoperator_types::page_type type) {
        return (static_cast<PageKey>(type) << 32) | static_cast<std::uint32_t>(pid);
    }

  public:
    // std::mutex buffer_pool_lock;

    // MUST declare the constructor here if you define it in the cpp
    buffer_pool(const std::string &db_filename, const std::string &index_filename, const std::string &wal_filename)
        : frames(buffer_manager_types::buffer_size), table(buffer_manager_types::buffer_size), replacement_check_queue(),
          disk_operator(db_filename, index_filename, wal_filename, buffer_manager_types::page_data_size), heap_filepath(db_filename),
          index_filepath(index_filename) {
        std::cout << "BUFFER CREATED";
    }

    [[nodiscard]] buffer_manager_types::Page *page_access(heap_page_types::page_id pid, diskoperator_types::page_type type);

    [[nodiscard]] buffer_manager_types::frame_id page_replacement_policy(diskoperator_types::page_type type);

    /*
      Acquire Read or Write Lock on Buffer Pool. Avoids contention by having a queue of locks, threads waits just like os locks.

      No implict unlock/lock is done like os condition variable.
    */
    void AcquireBufferPoolLock(std::unique_ptr<buffer_manager_types::BPLock> &thread);

    /*
     * Unlock the lock on buffer pool, though AcquireBufferPoolLock locks the mutex still references are passed and ownership remains with
     * the caller thread.
     */
    void UnlockBufferPoolLock(std::unique_ptr<buffer_manager_types::BPLock> &thread);

    void ProcessBufferPollLock();

    void un_pin(heap_page_types::page_id pid, diskoperator_types::page_type type);

    uintmax_t get_last_pid(diskoperator_types::page_type type);
    void      dp_write_to_wal(const wal_types::WAL_entry &record, const char *row_bytes);
    void      dp_write_page(buffer_manager_types::Page *page, diskoperator_types::page_type type);

    void dp_read_page(buffer_manager_types::Page *page, diskoperator_types::page_type type);

    void final_write();

    ~buffer_pool() {
        std::string message = "\nHeap Page written\n";
        write(STDOUT_FILENO, message.data(), message.size());
        final_write();
    }
};
}; // namespace buffer_manager

#endif
