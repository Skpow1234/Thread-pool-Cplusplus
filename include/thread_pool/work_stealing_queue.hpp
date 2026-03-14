#pragma once

#include "thread_pool/task.hpp"

#include <bit>      // std::bit_ceil
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace tp {

// WorkStealingQueue is a bounded, thread-safe double-ended queue (deque)
// designed for work-stealing thread pools.
//
// Access semantics
// ────────────────
//   Bottom end  (owner only) — LIFO stack.
//     push_bottom / pop_bottom: the owner always works on its most recent task.
//     Cache benefit: parent and child tasks stay on the same core.
//
//   Top end (any thread) — FIFO queue.
//     steal_top: thieves steal the oldest (largest) subtree, so one steal
//     grabs a big chunk of work and reduces how often they need to come back.
//
// Index arithmetic
// ────────────────
//   top_ and bottom_ are monotonically increasing uint64_t values.
//   Mapping to the circular buffer uses bitwise AND with mask_ (= capacity_-1),
//   which is equivalent to modulo but a single instruction.
//   Items occupy indices [top_, bottom_); the deque is empty when top_ == bottom_.
//
// Phase-A implementation (M7)
// ───────────────────────────
//   A single std::mutex serialises all three operations.
//   This is the correctness baseline; the lock-free Chase-Lev upgrade is M12.
//
// Invariants
// ──────────
//   - capacity_ is always a power of two.
//   - push_bottom() returns false without side effects when the buffer is full.
//   - pop_bottom() and steal_top() return false when empty.
//   - Non-copyable (owns a mutex and a live buffer).
class WorkStealingQueue {
public:
    // capacity_hint is rounded up to the next power of two (minimum 1).
    explicit WorkStealingQueue(std::size_t capacity_hint = 256)
        : capacity_{std::bit_ceil(std::max(capacity_hint, std::size_t{1}))}
        , mask_{capacity_ - 1}
        , buffer_(capacity_)
    {}

    // Owner pushes a task onto the bottom (LIFO end).
    // Returns false if the buffer is full; the task is left untouched.
    bool push_bottom(task_t task) {
        std::scoped_lock lock{mtx_};
        if (bottom_ - top_ >= capacity_) return false;
        buffer_[bottom_ & mask_] = std::move(task);
        ++bottom_;
        return true;
    }

    // Owner pops the most-recently-pushed task from the bottom (LIFO).
    // Returns false if empty.
    bool pop_bottom(task_t& out) {
        std::scoped_lock lock{mtx_};
        if (bottom_ == top_) return false;
        --bottom_;
        out = std::move(buffer_[bottom_ & mask_]);
        return true;
    }

    // Any thread may steal the oldest task from the top (FIFO).
    // Returns false if empty.
    bool steal_top(task_t& out) {
        std::scoped_lock lock{mtx_};
        if (bottom_ == top_) return false;
        out = std::move(buffer_[top_ & mask_]);
        ++top_;
        return true;
    }

    [[nodiscard]] std::size_t size() const {
        std::scoped_lock lock{mtx_};
        return static_cast<std::size_t>(bottom_ - top_);
    }

    [[nodiscard]] bool empty() const {
        std::scoped_lock lock{mtx_};
        return bottom_ == top_;
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    WorkStealingQueue(const WorkStealingQueue&)            = delete;
    WorkStealingQueue& operator=(const WorkStealingQueue&) = delete;

private:
    const std::size_t   capacity_;
    const std::size_t   mask_;      // capacity_ - 1; fast modulo via bitwise AND
    std::vector<task_t> buffer_;

    // Monotonically increasing; never reset. Wrapping is handled by mask_.
    std::uint64_t top_{0};
    std::uint64_t bottom_{0};

    mutable std::mutex mtx_;
};

} // namespace tp
