#pragma once

#include "../../proto/client_server_common.pb.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>

namespace worker_functions {

enum WORKER_STATE { IDLE, BUSY };

struct client {
    std::size_t fd;
    client_server_common::Request client_input;
};

struct Worker {
    std::uint8_t thread_id = 0;
    std::optional<client> client;
    WORKER_STATE state = IDLE;
    std::thread thread;
    std::mutex mut;
    std::condition_variable condvar;
    bool shutdown_requested = false;

    // Called with mut locked. Pending work is drained before shutdown exits.
    bool wait_for_request(std::unique_lock<std::mutex> &lock) {
        condvar.wait(lock, [&] {
            return shutdown_requested || state == BUSY;
        });
        return state == BUSY;
    }

    void request_shutdown() {
        {
            std::lock_guard<std::mutex> lock(mut);
            shutdown_requested = true;
        }
        condvar.notify_all();
    }
};

} // namespace worker_functions
