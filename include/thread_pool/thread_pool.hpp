#pragma once

#include "thread_pool/centralized_queue.hpp"

#include <cstddef>
#include <stop_token>
#include <thread>
#include <vector>

namespace tp {

// ThreadPool owns a fixed set of worker threads backed by a CentralizedQueue.
//
// Invariants:
//   - Worker count is fixed at construction and never changes.
//   - Workers are started in the constructor and stopped in the destructor.
//   - Destruction is always clean: std::jthread requests stop and joins
//     automatically, so ~ThreadPool() never blocks indefinitely.
//   - A pool with 0 workers is valid: tasks can be pushed but will never run.
class ThreadPool {
public:
    // Spawn num_threads worker threads.
    // Defaults to the hardware concurrency reported by the OS.
    explicit ThreadPool(
        std::size_t num_threads = std::thread::hardware_concurrency());

    // Destructor requests cooperative stop on every worker and joins them.
    // std::jthread does both automatically; no manual signalling needed.
    ~ThreadPool() = default;

    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&)                 = delete;
    ThreadPool& operator=(ThreadPool&&)      = delete;

private:
    // Each worker runs this loop until its stop_token is signalled.
    // M4: loops with yield() — no task execution yet.
    // M5: will pop and execute tasks from queue_.
    void worker_loop(std::stop_token stoken);

    CentralizedQueue          queue_;
    std::vector<std::jthread> workers_;
};

} // namespace tp
