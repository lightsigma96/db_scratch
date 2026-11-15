#include "../src/storage_manager/headers/buffer_manager.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

int main() {
    buffer_manager::buffer_pool buf_pool("heap.bin", "index.bin", "wal.bin");

    constexpr size_t THREADS = 10000;

    std::atomic<long long>   total_wait_us{0};
    std::vector<long long>   wait_times;
    std::mutex               wait_times_mutex;
    std::vector<std::thread> thread_pool;

    for (size_t i = 0; i < THREADS; i++) {
        thread_pool.emplace_back([&]() {
            auto bpl       = std::make_unique<buffer_manager_types::BPLock>();
            bpl->lock_type = buffer_manager_types::READ_LOCK;

            const auto s = std::chrono::steady_clock::now();

            buf_pool.AcquireBufferPoolLock(bpl);

            const auto e = std::chrono::steady_clock::now();

            const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(e - s).count();

            total_wait_us.fetch_add(duration);

            {
                std::lock_guard<std::mutex> lock(wait_times_mutex);
                wait_times.push_back(duration);
            }

            auto p = buf_pool.page_access(0, diskoperator_types::HEAP_PAGE);
        });
    }

    for (auto &t : thread_pool) {
        t.join();
    }

    std::sort(wait_times.begin(), wait_times.end());

    const double average = static_cast<double>(total_wait_us.load()) / THREADS;

    double median;

    if (THREADS % 2 == 0) {
        median = (wait_times[THREADS / 2 - 1] + wait_times[THREADS / 2]) / 2.0;
    } else {
        median = wait_times[THREADS / 2];
    }

    std::cout << "Threads: " << THREADS << "\n";
    std::cout << "Average wait time: " << average << " us\n";
    std::cout << "Median wait time:  " << median << " us\n";
}
