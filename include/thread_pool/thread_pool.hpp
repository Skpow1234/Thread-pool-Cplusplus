#pragma once

#include "thread_pool/centralized_queue.hpp"

#include <concepts>
#include <cstddef>
#include <future>
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
//   - Tasks queued but not yet started when the pool is destroyed will never
//     run; their futures will throw std::future_error (broken_promise).
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

    // Submit a callable and return a future for its result.
    //
    // F must be invocable with no arguments (std::invocable<F>).
    // The return type R = std::invoke_result_t<F> may be void.
    //
    // std::packaged_task<R()> is move-only; wrapping it in a move-capturing
    // lambda produces a move-only callable that std::move_only_function
    // accepts directly — no shared_ptr or heap indirection needed.
    template <std::invocable F>
    auto submit(F&& f) -> std::future<std::invoke_result_t<F>> {
        using R = std::invoke_result_t<F>;

        std::packaged_task<R()> task{std::forward<F>(f)};
        auto future = task.get_future();

        queue_.push([t = std::move(task)]() mutable { t(); });

        return future;
    }

private:
    // Each worker pops tasks from queue_ and executes them until stopped.
    void worker_loop(std::stop_token stoken);

    CentralizedQueue          queue_;
    std::vector<std::jthread> workers_;
};

} // namespace tp
