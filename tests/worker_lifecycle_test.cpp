#include "../src/transaction_manager/worker_state.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <mutex>

namespace {
using namespace std::chrono_literals;
}

TEST(WorkerLifecycleTest, WaitsWhileIdleUntilWorkIsQueued) {
    worker_functions::Worker worker;
    auto waiting = std::async(std::launch::async, [&] {
        std::unique_lock<std::mutex> lock(worker.mut);
        return worker.wait_for_request(lock);
    });

    EXPECT_EQ(waiting.wait_for(75ms), std::future_status::timeout);
    {
        std::lock_guard<std::mutex> lock(worker.mut);
        worker.state = worker_functions::BUSY;
    }
    worker.condvar.notify_one();

    ASSERT_EQ(waiting.wait_for(1s), std::future_status::ready);
    EXPECT_TRUE(waiting.get());
}

TEST(WorkerLifecycleTest, SpuriousNotificationDoesNotReleaseAnIdleWorker) {
    worker_functions::Worker worker;
    auto waiting = std::async(std::launch::async, [&] {
        std::unique_lock<std::mutex> lock(worker.mut);
        return worker.wait_for_request(lock);
    });

    worker.condvar.notify_all();
    EXPECT_EQ(waiting.wait_for(75ms), std::future_status::timeout);

    {
        std::lock_guard<std::mutex> lock(worker.mut);
        worker.state = worker_functions::BUSY;
    }
    worker.condvar.notify_one();

    ASSERT_EQ(waiting.wait_for(1s), std::future_status::ready);
    EXPECT_TRUE(waiting.get());
}

TEST(WorkerLifecycleTest, ShutdownWakesAnIdleWorker) {
    worker_functions::Worker worker;
    auto waiting = std::async(std::launch::async, [&] {
        std::unique_lock<std::mutex> lock(worker.mut);
        return worker.wait_for_request(lock);
    });

    EXPECT_EQ(waiting.wait_for(75ms), std::future_status::timeout);
    worker.request_shutdown();

    ASSERT_EQ(waiting.wait_for(1s), std::future_status::ready);
    EXPECT_FALSE(waiting.get());
}

TEST(WorkerLifecycleTest, QueuedWorkIsDrainedBeforeShutdown) {
    worker_functions::Worker worker;
    {
        std::lock_guard<std::mutex> lock(worker.mut);
        worker.state = worker_functions::BUSY;
    }
    worker.request_shutdown();

    std::unique_lock<std::mutex> lock(worker.mut);
    EXPECT_TRUE(worker.wait_for_request(lock));
}

TEST(WorkerLifecycleTest, ShutdownWithoutQueuedWorkReturnsFalse) {
    worker_functions::Worker worker;
    worker.request_shutdown();

    std::unique_lock<std::mutex> lock(worker.mut);
    EXPECT_FALSE(worker.wait_for_request(lock));
}
