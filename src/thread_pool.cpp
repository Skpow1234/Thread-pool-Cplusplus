#include "thread_pool/thread_pool.hpp"

#include <thread>

namespace tp {

ThreadPool::ThreadPool(std::size_t num_threads) {
    workers_.reserve(num_threads);
    for (std::size_t i = 0; i < num_threads; ++i) {
        // std::jthread passes a stop_token as the first argument automatically
        // when the callable accepts one.
        workers_.emplace_back([this](std::stop_token stoken) {
            worker_loop(std::move(stoken));
        });
    }
}

void ThreadPool::worker_loop(std::stop_token stoken) {
    task_t task;
    while (!stoken.stop_requested()) {
        if (queue_.try_pop(task)) {
            task();
        } else {
            // No work available: yield to the OS scheduler rather than
            // burning the core. The cost is slightly higher wakeup latency
            // compared to a condition_variable::wait, which is an accepted
            // trade-off for the active-polling design (see docs/DESIGN.md).
            std::this_thread::yield();
        }
    }
}

} // namespace tp
