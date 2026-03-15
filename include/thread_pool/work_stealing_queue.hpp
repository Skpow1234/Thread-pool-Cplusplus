#pragma once

#include "thread_pool/task.hpp"

#include <atomic>
#include <bit>      // std::bit_ceil
#include <cstddef>
#include <cstdint>
#include <new>      // std::hardware_destructive_interference_size
#include <vector>

namespace tp {

// WorkStealingQueue — lock-free Chase-Lev work-stealing deque (M12).
//
// This is an implementation of the algorithm described in:
//   Chase & Lev, "Dynamic circular work-stealing deque", SPAA 2005.
//
// Access semantics
// ────────────────
//   Bottom end  (owner only, LIFO stack)
//     push_bottom / pop_bottom: the owner always works on its most recent task.
//     Cache benefit: parent and child tasks share hot cache lines.
//
//   Top end (any thread, FIFO queue)
//     steal_top: thieves steal the oldest task.  One steal grabs a large
//     subtree of work, reducing how often thieves need to revisit.
//
// Index arithmetic
// ────────────────
//   top_ and bottom_ are monotonically increasing std::atomic<uint64_t>.
//   They are never reset; wrapping is done by bitwise AND with mask_
//   (mask_ = capacity_ - 1), which is equivalent to modulo for powers of two.
//   Items occupy indices [top_, bottom_); the deque is empty when top_ == bottom_.
//
// Lock-free design (M12)
// ──────────────────────
//   The mutex is replaced by atomic operations and memory fences.  The key
//   insight is that the owner is the only writer of bottom_, so it can use a
//   relaxed store there (with a seq_cst fence to synchronise with thieves).
//   Thieves advance top_ via compare_exchange to resolve the "last element" race.
//
//   CAS-before-move invariant
//   ─────────────────────────
//   Unlike naive lock-free deques, we move the task out of the buffer ONLY AFTER
//   winning ownership via a CAS or after establishing sole access (non-last-element
//   path in pop_bottom).  This is essential for move-only types such as task_t: two
//   threads must never both call std::move on the same buffer slot.
//
// Cache-line layout (M9, preserved in M12)
// ─────────────────────────────────────────
//   top_ and bottom_ are on separate cache lines (alignas).  Concurrent writes
//   to these variables by different cores do not invalidate each other's cache
//   lines.  MSVC warning C4324 is suppressed in CMakeLists.txt (/wd4324).
//
// Invariants
// ──────────
//   - capacity_ is always a power of two.
//   - push_bottom() returns false without modifying state when the buffer is full.
//   - pop_bottom() and steal_top() return false when empty.
//   - Non-copyable and non-movable (a live deque is referenced by worker threads).
class WorkStealingQueue {
public:
    // capacity_hint is rounded up to the next power of two (minimum 1).
    explicit WorkStealingQueue(std::size_t capacity_hint = 256)
        : capacity_{std::bit_ceil(std::max(capacity_hint, std::size_t{1}))}
        , mask_{capacity_ - 1}
        , buffer_(capacity_)
    {}

    WorkStealingQueue(const WorkStealingQueue&)            = delete;
    WorkStealingQueue& operator=(const WorkStealingQueue&) = delete;
    WorkStealingQueue(WorkStealingQueue&&)                 = delete;
    WorkStealingQueue& operator=(WorkStealingQueue&&)      = delete;
    ~WorkStealingQueue()                                   = default;

    // ── push_bottom ───────────────────────────────────────────────────────────
    // Owner pushes a task onto the bottom (LIFO end).
    // Returns false if the buffer is full; the task is left untouched.
    //
    // Memory ordering:
    //   (1) bottom_.load  relaxed  — only the owner ever writes bottom_; no
    //                                concurrent writer means no ordering needed.
    //   (2) top_.load     acquire  — syncs with any successful CAS that advanced
    //                                top_ (from steal_top or the owner's own CAS
    //                                in pop_bottom).  Without acquire we might see
    //                                a stale top_ and wrongly think the buffer full.
    //   (3) bottom_.store release  — publishes the buffer write.  Any thread that
    //                                acquire-loads bottom_ in steal_top will see the
    //                                task we just wrote, establishing happens-before
    //                                between the push and the steal.
    [[nodiscard]] bool push_bottom(task_t task) {
        const std::uint64_t b = bottom_.load(std::memory_order_relaxed);  // (1)
        const std::uint64_t t = top_.load(std::memory_order_acquire);     // (2)
        if (b - t >= capacity_) { return false; }
        buffer_[b & mask_] = std::move(task);
        bottom_.store(b + 1, std::memory_order_release);                  // (3)
        return true;
    }

    // ── pop_bottom ────────────────────────────────────────────────────────────
    // Owner pops the most-recently-pushed task from the bottom (LIFO).
    // Returns false if empty.
    //
    // Underflow guard
    // ───────────────
    //   top_ and bottom_ are uint64_t.  Decrementing bottom_ when the deque is
    //   empty (bottom_ == top_) would produce UINT64_MAX via unsigned wraparound.
    //   The subsequent "t > b" empty-check (t=0, b=UINT64_MAX) would then be
    //   false, causing pop_bottom to incorrectly claim the queue is non-empty.
    //
    //   Fix: load the current bottom_ first; if it equals the current top_
    //   (observationally empty), return false before touching bottom_.
    //   This pre-check is safe because only the owner writes bottom_ — the
    //   relaxed load gives the definitive current value.
    //
    // Memory ordering:
    //   (1) bottom_.load relaxed — owner-private; true current value.
    //   (2) top_.load   acquire  — pre-check: syncs with any CAS that advanced
    //                              top_ so we don't decrement on a stale empty.
    //                              If b_cur == t_pre the deque is empty; return.
    //   (3) bottom_.store relaxed — speculative decrement; ordering via fence.
    //   (4) atomic_thread_fence seq_cst — pairs with the matching fence in
    //                              steal_top.  After this fence the thief sees
    //                              bottom_ = b and we see any advance of top_.
    //   (5) top_.load  relaxed  — safe after the seq_cst fence.
    //   (6) top_.compare_exchange seq_cst — wins sole ownership of the last
    //       element.  CAS-before-move: move only after winning.
    [[nodiscard]] bool pop_bottom(task_t& out) {
        const std::uint64_t b_cur = bottom_.load(std::memory_order_relaxed); // (1)

        // Pre-check: fast-path return for the empty case to avoid uint64 underflow.
        if (b_cur == top_.load(std::memory_order_acquire)) { return false; } // (2)

        const std::uint64_t b = b_cur - 1;  // safe: b_cur >= 1 (b_cur > top_ >= 0)
        bottom_.store(b, std::memory_order_relaxed);                      // (3) speculative decrement

        std::atomic_thread_fence(std::memory_order_seq_cst);             // (4)

        std::uint64_t t = top_.load(std::memory_order_relaxed);          // (5)

        if (t > b) {
            // A thief stole all remaining elements between our pre-check and now.
            bottom_.store(b + 1, std::memory_order_relaxed);
            return false;
        }

        if (t == b) {
            // Last element: the owner and any thief race via CAS on top_.
            //
            // CAS-before-move: we do NOT move from the buffer before winning,
            // ensuring two racing threads never both call std::move on the slot.
            const bool won = top_.compare_exchange_strong(               // (6)
                t, t + 1,
                std::memory_order_seq_cst,   // success: total order with thieves
                std::memory_order_relaxed);  // failure: we discard t anyway

            // Restore bottom_ regardless of outcome.  If we won the queue is
            // now empty (top_ == bottom_ == b+1).  If we lost it was already.
            bottom_.store(b + 1, std::memory_order_relaxed);

            if (!won) { return false; }
        }

        // Sole owner of slot b: either multiple elements existed (no race) or
        // we won the last-element CAS above.  Safe to move now.
        out = std::move(buffer_[b & mask_]);
        return true;
    }

    // ── steal_top ─────────────────────────────────────────────────────────────
    // Any thread may steal the oldest task from the top (FIFO).
    // Returns false if empty or if another thread won the CAS race.
    //
    // Memory ordering:
    //   (1) top_.load    acquire  — syncs with any previous successful CAS on
    //                               top_, ensuring we see the latest "stolen-up-to"
    //                               index and don't try to steal an already-gone slot.
    //   (2) atomic_thread_fence seq_cst — pairs with the fence in pop_bottom.
    //                               Together they form a single point in the total
    //                               seq_cst order.  Guarantees that if the owner
    //                               decremented bottom_ before our fence, we will see
    //                               the new value in (3) and treat the deque as empty.
    //   (3) bottom_.load acquire  — syncs with push_bottom's release store, making
    //                               the task written at buffer_[b-1 & mask_] visible.
    //                               Also provides the up-to-date bottom_ for the
    //                               non-empty check.
    //   (4) top_.compare_exchange seq_cst — atomically claims slot t.  Only the
    //       CAS winner proceeds to move from the buffer.  failure=relaxed because
    //       t is reloaded by the caller on the next attempt.
    [[nodiscard]] bool steal_top(task_t& out) {
        std::uint64_t t = top_.load(std::memory_order_acquire);           // (1)

        std::atomic_thread_fence(std::memory_order_seq_cst);             // (2)

        const std::uint64_t b = bottom_.load(std::memory_order_acquire); // (3)
        if (t >= b) { return false; }

        // CAS-before-move: claim ownership of slot t before reading the buffer.
        // Without this, two thieves could both move from the same slot; one would
        // get a valid task_t and the other a moved-from (empty) function object.
        if (!top_.compare_exchange_strong(                                // (4)
                t, t + 1,
                std::memory_order_seq_cst,   // success: total order with pop_bottom
                std::memory_order_relaxed)) {
            return false;
        }

        // We exclusively own slot t.  The acquire load of bottom_ in (3) synced
        // with push_bottom's release store, so the task is visible to us.
        out = std::move(buffer_[t & mask_]);
        return true;
    }

    // ── Observers ─────────────────────────────────────────────────────────────
    // size() and empty() are advisory: the snapshot may be slightly stale
    // during concurrent push/pop/steal.  They are used only for diagnostics
    // and the full-check inside push_bottom; never for control flow.

    [[nodiscard]] std::size_t size() const {
        // Load bottom_ first (acquire) to see the latest push; then top_
        // (acquire) to see the latest steal or pop.  b < t is transiently
        // possible during pop_bottom's speculative decrement, hence the guard.
        const auto b = bottom_.load(std::memory_order_acquire);
        const auto t = top_.load(std::memory_order_acquire);
        return b > t ? static_cast<std::size_t>(b - t) : 0U;
    }

    [[nodiscard]] bool empty() const {
        const auto t = top_.load(std::memory_order_acquire);
        const auto b = bottom_.load(std::memory_order_acquire);
        return t >= b;
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

private:
    // Cold data: set at construction, never modified again.
    const std::size_t   capacity_;
    const std::size_t   mask_;       // capacity_ - 1; fast modulo via bitwise AND
    std::vector<task_t> buffer_;

    // Hot indices — each on its own cache line (M9/M12).
    //
    // top_    is written by thieves (steal_top CAS) and by the owner's own CAS
    //         in pop_bottom when racing for the last element.
    // bottom_ is written exclusively by the owner (push_bottom / pop_bottom).
    //
    // Placing them on separate cache lines eliminates false sharing: a write to
    // bottom_ no longer invalidates the thief's cached copy of top_, and vice
    // versa.  MSVC warning C4324 is suppressed via /wd4324 in CMakeLists.txt.
    alignas(std::hardware_destructive_interference_size)
    std::atomic<std::uint64_t> top_{0};

    alignas(std::hardware_destructive_interference_size)
    std::atomic<std::uint64_t> bottom_{0};
};

} // namespace tp
