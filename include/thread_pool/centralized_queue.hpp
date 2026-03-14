#pragma once

#include "thread_pool/task.hpp"

#include <cstddef>
#include <mutex>
#include <queue>

namespace tp {

// CentralizedQueue is a thread-safe, unbounded MPMC task queue.
//
// This is the Phase-A queue used before work-stealing is introduced in M8.
// A single std::mutex serialises all push/pop operations, which is the
// known bottleneck that the work-stealing design later eliminates.
//
// Invariants:
//   - push() always succeeds (queue is unbounded).
//   - try_pop() never blocks; it returns false immediately when empty.
//   - size() and empty() reflect a consistent snapshot taken under the lock,
//     but may be stale by the time the caller acts on the value.
class CentralizedQueue {
public:
    // Takes ownership of the task (move-only; copies are intentionally rejected).
    void push(task_t task) {
        std::scoped_lock lock{mtx_};
        queue_.push(std::move(task));
    }

    // Moves the front task into `out` and returns true.
    // Returns false without modifying `out` if the queue is empty.
    bool try_pop(task_t& out) {
        std::scoped_lock lock{mtx_};
        if (queue_.empty()) return false;
        out = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    [[nodiscard]] std::size_t size() const {
        std::scoped_lock lock{mtx_};
        return queue_.size();
    }

    [[nodiscard]] bool empty() const {
        std::scoped_lock lock{mtx_};
        return queue_.empty();
    }

    CentralizedQueue()                                   = default;
    CentralizedQueue(const CentralizedQueue&)            = delete;
    CentralizedQueue& operator=(const CentralizedQueue&) = delete;

private:
    mutable std::mutex  mtx_;
    std::queue<task_t>  queue_;
};

} // namespace tp
