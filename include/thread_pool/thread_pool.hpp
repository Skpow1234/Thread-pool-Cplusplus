#pragma once

#include "thread_pool/centralized_queue.hpp"
#include "thread_pool/work_stealing_queue.hpp"

#include <concepts>
#include <cstddef>
#include <future>
#include <memory>
#include <stop_token>
#include <thread>
#include <vector>

namespace tp {

// ThreadPool — work-stealing thread pool (Phase B).
//
// Architecture
// ────────────
//   Each worker owns a local WorkStealingQueue (per-thread deque).
//   A CentralizedQueue serves as a global overflow queue for tasks submitted
//   by external (non-worker) threads, or when a local deque is full.
//
// Worker loop priority (highest → lowest):
//   1. Pop from own deque          (no contention, LIFO, cache-friendly)
//   2. Pop from global overflow    (one mutex, rare for pure CPU workloads)
//   3. Steal from a peer's deque   (round-robin, FIFO, one mutex per victim)
//   4. yield()                     (back-off when truly idle)
//
// submit() fast path / slow path
// ────────────────────────────────
//   Fast path (caller is a worker thread):
//     Push to local_queue_ — no global synchronisation.
//     If local deque is full, fall back to global queue.
//   Slow path (external thread):
//     Push to global CentralizedQueue under its own mutex.
//
// Destruction order invariant
// ───────────────────────────
//   Members are destroyed in reverse declaration order.
//   workers_ is declared last → destroyed first (jthreads stop & join).
//   This guarantees workers never access worker_queues_ after it is freed.
//
// Invariants
// ──────────
//   - Worker count is fixed at construction; never resized.
//   - local_queue_ is null for any thread not owned by this pool.
//   - Tasks queued but unstarted when the pool destructs will never run;
//     their futures throw std::future_error (broken_promise) on get().
class ThreadPool {
public:
    explicit ThreadPool(
        std::size_t num_threads = std::thread::hardware_concurrency());

    ~ThreadPool() = default;

    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&)                 = delete;
    ThreadPool& operator=(ThreadPool&&)      = delete;

    // Submit a callable; returns a future for its result.
    //
    // Fast path  — caller is a worker thread:  push to local deque.
    // Slow path  — caller is external thread:  push to global queue.
    //
    // [[nodiscard]]: dropping the future abandons exception propagation and
    // makes it impossible to observe task completion.
    template <std::invocable F>
    [[nodiscard]] auto submit(F&& f) -> std::future<std::invoke_result_t<F>> {
        using R = std::invoke_result_t<F>;

        std::packaged_task<R()> ptask{std::forward<F>(f)};
        auto future = ptask.get_future();

        task_t wrapped = [t = std::move(ptask)]() mutable { t(); };

        if (local_queue_ != nullptr) {
            // Fast path: zero global synchronisation.
            // If the local deque is full (unlikely), overflow to global queue.
            if (!local_queue_->push_bottom(std::move(wrapped))) {
                queue_.push(std::move(wrapped));
            }
        } else {
            queue_.push(std::move(wrapped));
        }

        return future;
    }

private:
    void worker_loop(std::stop_token stoken, std::size_t idx);
    static bool pop_from_local(task_t& out);   // uses only static thread_local state
    bool pop_from_global(task_t& out);
    bool steal_from_peers(task_t& out);

    // Thread-local pointers set by each worker on startup.
    // Null for any thread not owned by this pool.
    static thread_local WorkStealingQueue* local_queue_;
    static thread_local std::size_t        thread_idx_;

    // Declared before workers_ so deques outlive worker threads.
    std::vector<std::unique_ptr<WorkStealingQueue>> worker_queues_;
    CentralizedQueue                                queue_;     // global overflow
    std::vector<std::jthread>                       workers_;  // destroyed first
};

} // namespace tp
