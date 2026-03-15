#include "thread_pool/thread_pool.hpp"

#include <thread>

namespace tp {

// Thread-local state — null/0 for any thread not owned by a ThreadPool.
thread_local WorkStealingQueue* ThreadPool::local_queue_ = nullptr;
thread_local std::size_t        ThreadPool::thread_idx_  = 0;

// Per-worker deque capacity (rounded up to next power of two by the deque).
// External submissions always go to the unbounded global queue, so this only
// matters when worker threads themselves call submit() (recursive workloads).
static constexpr std::size_t kLocalDequeCapacity = 1024;

ThreadPool::ThreadPool(std::size_t num_threads) {
    // Deques MUST exist before any worker thread starts — workers capture a
    // raw pointer to their deque on first entry to worker_loop.
    worker_queues_.reserve(num_threads);
    for (std::size_t i = 0; i < num_threads; ++i) {
        worker_queues_.push_back(
            std::make_unique<WorkStealingQueue>(kLocalDequeCapacity));
    }

    workers_.reserve(num_threads);
    for (std::size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this, i](std::stop_token stoken) {
            worker_loop(std::move(stoken), i);
        });
    }
}

void ThreadPool::worker_loop(std::stop_token stoken, std::size_t idx) {
    // Bind this thread's local state once on entry.
    local_queue_ = worker_queues_[idx].get();
    thread_idx_  = idx;

    task_t task;
    while (!stoken.stop_requested()) {
        if (pop_from_local(task) ||
            pop_from_global(task) ||
            steal_from_peers(task)) {
            task();
        } else {
            std::this_thread::yield();
        }
    }
}

bool ThreadPool::pop_from_local(task_t& out) {
    // local_queue_ is always non-null inside worker_loop, but the guard
    // keeps this callable safely from any context.
    return local_queue_ != nullptr && local_queue_->pop_bottom(out);
}

bool ThreadPool::pop_from_global(task_t& out) {
    return queue_.try_pop(out);
}

bool ThreadPool::steal_from_peers(task_t& out) {
    // Round-robin starting from the next worker so we don't always favour
    // the same victim. Starting at i=1 skips our own queue (already checked).
    const std::size_t n = worker_queues_.size();
    for (std::size_t i = 1; i < n; ++i) {
        const std::size_t target = (thread_idx_ + i) % n;
        if (worker_queues_[target]->steal_top(out)) return true;
    }
    return false;
}

} // namespace tp
