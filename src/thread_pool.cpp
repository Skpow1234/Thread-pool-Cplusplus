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
    // M4: workers idle-loop with yield() until stop is requested.
    // yield() hints to the OS to reschedule this thread, so we don't
    // burn a core while waiting. Task execution is added in M5.
    while (!stoken.stop_requested()) {
        std::this_thread::yield();
    }
}

} // namespace tp
