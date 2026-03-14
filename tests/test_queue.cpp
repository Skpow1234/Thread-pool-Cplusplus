#include <gtest/gtest.h>

#include "thread_pool/centralized_queue.hpp"
#include "thread_pool/work_stealing_queue.hpp"

#include <atomic>
#include <thread>
#include <vector>

using tp::CentralizedQueue;
using tp::task_t;

// CentralizedQueue must not be copyable (it owns a mutex).
static_assert(!std::is_copy_constructible_v<CentralizedQueue>);
static_assert(!std::is_copy_assignable_v<CentralizedQueue>);

// ---------------------------------------------------------------------------
// Single-threaded: basic push / try_pop round-trip.
// ---------------------------------------------------------------------------
TEST(CentralizedQueue, SingleThreadRoundTrip) {
    CentralizedQueue q;

    bool called = false;
    q.push([&called] { called = true; });

    ASSERT_EQ(q.size(), 1u);
    ASSERT_FALSE(q.empty());

    task_t t;
    ASSERT_TRUE(q.try_pop(t));
    ASSERT_TRUE(q.empty());

    t();
    ASSERT_TRUE(called);
}

// ---------------------------------------------------------------------------
// Single-threaded: try_pop on an empty queue returns false immediately.
// ---------------------------------------------------------------------------
TEST(CentralizedQueue, TryPopOnEmptyReturnsFalse) {
    CentralizedQueue q;

    task_t t;
    ASSERT_FALSE(q.try_pop(t));
}

// ---------------------------------------------------------------------------
// Single-threaded: tasks come out in FIFO order.
// ---------------------------------------------------------------------------
TEST(CentralizedQueue, FIFOOrder) {
    CentralizedQueue q;
    std::vector<int> order;

    q.push([&order] { order.push_back(1); });
    q.push([&order] { order.push_back(2); });
    q.push([&order] { order.push_back(3); });

    task_t t;
    while (q.try_pop(t)) t();

    ASSERT_EQ(order, (std::vector<int>{1, 2, 3}));
}

// ---------------------------------------------------------------------------
// Multi-threaded: N producers push tasks; N consumers pop and execute them.
// Every task increments an atomic counter exactly once, so the final value
// must equal the total number of tasks submitted.
//
// Strategy:
//   Phase 1 — all producers push their tasks and join (queue is fully loaded).
//   Phase 2 — all consumers drain the queue and join.
// No task can be missed because the queue is already full before any consumer
// starts trying to pop.
// ---------------------------------------------------------------------------
TEST(CentralizedQueue, MPMCAllTasksConsumedExactlyOnce) {
    constexpr int kProducers       = 8;
    constexpr int kConsumers       = 8;
    constexpr int kTasksPerProducer = 10'000;
    constexpr int kTotal           = kProducers * kTasksPerProducer;

    CentralizedQueue q;
    std::atomic<int> counter{0};

    // Phase 1: fill the queue.
    {
        std::vector<std::jthread> producers;
        producers.reserve(kProducers);
        for (int i = 0; i < kProducers; ++i) {
            producers.emplace_back([&] {
                for (int j = 0; j < kTasksPerProducer; ++j) {
                    q.push([&counter] {
                        counter.fetch_add(1, std::memory_order_relaxed);
                    });
                }
            });
        }
    } // all producers joined here

    ASSERT_EQ(static_cast<int>(q.size()), kTotal);

    // Phase 2: drain the queue.
    {
        std::vector<std::jthread> consumers;
        consumers.reserve(kConsumers);
        for (int i = 0; i < kConsumers; ++i) {
            consumers.emplace_back([&] {
                task_t t;
                while (q.try_pop(t)) t();
            });
        }
    } // all consumers joined here

    ASSERT_EQ(counter.load(std::memory_order_relaxed), kTotal);
    ASSERT_TRUE(q.empty());
}

// ---------------------------------------------------------------------------
// Multi-threaded: concurrent push and pop overlap (no phase separation).
// Verifies there are no deadlocks or crashes under interleaved access.
// ---------------------------------------------------------------------------
TEST(CentralizedQueue, ConcurrentPushPopNoDeadlock) {
    constexpr int kThreads        = 4;
    constexpr int kTasksPerThread = 5'000;
    constexpr int kTotal          = kThreads * kTasksPerThread;

    CentralizedQueue q;
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};

    std::vector<std::jthread> threads;
    threads.reserve(kThreads * 2);

    // Producers and consumers run simultaneously.
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&] {
            for (int j = 0; j < kTasksPerThread; ++j) {
                q.push([&consumed] {
                    consumed.fetch_add(1, std::memory_order_relaxed);
                });
                produced.fetch_add(1, std::memory_order_relaxed);
            }
        });

        threads.emplace_back([&] {
            task_t t;
            int local = 0;
            // Each consumer thread drains whatever it can find.
            while (local < kTasksPerThread) {
                if (q.try_pop(t)) { t(); ++local; }
                else               std::this_thread::yield();
            }
        });
    }

    // Join all.
    threads.clear();

    // Drain any tasks left (producers may have outrun consumers).
    {
        task_t t;
        while (q.try_pop(t)) t();
    }

    ASSERT_EQ(produced.load(), kTotal);
    ASSERT_EQ(consumed.load(), kTotal);
}

// ===========================================================================
// M7: WorkStealingQueue — locked circular-buffer deque.
// ===========================================================================

using tp::WorkStealingQueue;

static_assert(!std::is_copy_constructible_v<WorkStealingQueue>);
static_assert(!std::is_copy_assignable_v<WorkStealingQueue>);

// ---------------------------------------------------------------------------
// Capacity is always rounded up to the next power of two.
// ---------------------------------------------------------------------------
TEST(WorkStealingQueue, CapacityIsPowerOfTwo) {
    ASSERT_EQ(WorkStealingQueue{1}.capacity(),   1u);
    ASSERT_EQ(WorkStealingQueue{2}.capacity(),   2u);
    ASSERT_EQ(WorkStealingQueue{3}.capacity(),   4u);
    ASSERT_EQ(WorkStealingQueue{5}.capacity(),   8u);
    ASSERT_EQ(WorkStealingQueue{16}.capacity(), 16u);
    ASSERT_EQ(WorkStealingQueue{17}.capacity(), 32u);
}

// ---------------------------------------------------------------------------
// pop_bottom on an empty deque returns false immediately.
// ---------------------------------------------------------------------------
TEST(WorkStealingQueue, PopBottomOnEmptyReturnsFalse) {
    WorkStealingQueue q{8};
    task_t t;
    ASSERT_FALSE(q.pop_bottom(t));
}

// ---------------------------------------------------------------------------
// steal_top on an empty deque returns false immediately.
// ---------------------------------------------------------------------------
TEST(WorkStealingQueue, StealTopOnEmptyReturnsFalse) {
    WorkStealingQueue q{8};
    task_t t;
    ASSERT_FALSE(q.steal_top(t));
}

// ---------------------------------------------------------------------------
// Owner push/pop follows LIFO order (stack discipline).
// push A, B, C → pop C, B, A
// ---------------------------------------------------------------------------
TEST(WorkStealingQueue, OwnerLIFOOrder) {
    WorkStealingQueue q{8};
    std::vector<int> order;

    q.push_bottom([&order] { order.push_back(1); });
    q.push_bottom([&order] { order.push_back(2); });
    q.push_bottom([&order] { order.push_back(3); });

    ASSERT_EQ(q.size(), 3u);

    task_t t;
    while (q.pop_bottom(t)) t();

    ASSERT_EQ(order, (std::vector<int>{3, 2, 1}));
    ASSERT_TRUE(q.empty());
}

// ---------------------------------------------------------------------------
// steal_top follows FIFO order (queue discipline from the other end).
// push A, B, C → steal A, B, C
// ---------------------------------------------------------------------------
TEST(WorkStealingQueue, ThiefFIFOOrder) {
    WorkStealingQueue q{8};
    std::vector<int> order;

    q.push_bottom([&order] { order.push_back(1); });
    q.push_bottom([&order] { order.push_back(2); });
    q.push_bottom([&order] { order.push_back(3); });

    task_t t;
    while (q.steal_top(t)) t();

    ASSERT_EQ(order, (std::vector<int>{1, 2, 3}));
    ASSERT_TRUE(q.empty());
}

// ---------------------------------------------------------------------------
// push_bottom returns false (not UB) when the buffer is full.
// The failed push must not enqueue the task or corrupt state.
// ---------------------------------------------------------------------------
TEST(WorkStealingQueue, PushWhenFullReturnsFalse) {
    WorkStealingQueue q{4};  // capacity = 4 (already a power of two)
    ASSERT_EQ(q.capacity(), 4u);

    ASSERT_TRUE(q.push_bottom([] {}));
    ASSERT_TRUE(q.push_bottom([] {}));
    ASSERT_TRUE(q.push_bottom([] {}));
    ASSERT_TRUE(q.push_bottom([] {}));

    // One more push must be rejected.
    ASSERT_FALSE(q.push_bottom([] {}));

    // Existing items are intact.
    ASSERT_EQ(q.size(), 4u);

    // Drain should succeed exactly 4 times.
    int count = 0;
    task_t t;
    while (q.pop_bottom(t)) ++count;
    ASSERT_EQ(count, 4);
    ASSERT_TRUE(q.empty());
}

// ---------------------------------------------------------------------------
// Circular wrap-around: push, pop, push again past the original capacity
// boundary to verify the index arithmetic wraps correctly.
// ---------------------------------------------------------------------------
TEST(WorkStealingQueue, IndexWrapsAround) {
    WorkStealingQueue q{4};

    // Fill and drain twice to force index wrap.
    for (int round = 0; round < 2; ++round) {
        std::vector<int> order;
        for (int i = 1; i <= 4; ++i)
            ASSERT_TRUE(q.push_bottom([i, &order] { order.push_back(i); }));

        task_t t;
        while (q.steal_top(t)) t();

        ASSERT_EQ(order, (std::vector<int>{1, 2, 3, 4}));
        ASSERT_TRUE(q.empty());
    }
}

// ---------------------------------------------------------------------------
// 4 thief threads concurrently drain a pre-filled deque.
// Every task must execute exactly once; no task lost, no double-execution.
// ---------------------------------------------------------------------------
TEST(WorkStealingQueue, ConcurrentStealAllTasksExactlyOnce) {
    constexpr std::size_t kTasks   = 4'000;
    constexpr int         kThieves = 4;

    WorkStealingQueue q{kTasks};
    std::atomic<int> counter{0};

    // Phase 1: single owner fills the deque (no concurrency yet).
    for (std::size_t i = 0; i < kTasks; ++i) {
        ASSERT_TRUE(q.push_bottom([&counter] {
            counter.fetch_add(1, std::memory_order_relaxed);
        }));
    }
    ASSERT_EQ(q.size(), kTasks);

    // Phase 2: thieves drain concurrently.
    {
        std::vector<std::jthread> thieves;
        thieves.reserve(kThieves);
        for (int i = 0; i < kThieves; ++i) {
            thieves.emplace_back([&q] {
                task_t t;
                while (q.steal_top(t)) t();
            });
        }
    } // all thieves joined here

    ASSERT_EQ(counter.load(std::memory_order_relaxed), static_cast<int>(kTasks));
    ASSERT_TRUE(q.empty());
}

// ---------------------------------------------------------------------------
// Owner pops from bottom while thieves steal from top simultaneously.
// Total executions must equal total pushed; no task skipped or doubled.
// ---------------------------------------------------------------------------
TEST(WorkStealingQueue, ConcurrentOwnerPopAndThiefSteal) {
    constexpr std::size_t kTasks   = 2'000;
    constexpr int         kThieves = 3;

    WorkStealingQueue q{kTasks};
    std::atomic<int> counter{0};

    // Fill deque before starting any threads.
    for (std::size_t i = 0; i < kTasks; ++i) {
        q.push_bottom([&counter] {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    // Launch thieves first, then owner pops — all run concurrently.
    std::vector<std::jthread> thieves;
    thieves.reserve(kThieves);
    for (int i = 0; i < kThieves; ++i) {
        thieves.emplace_back([&q] {
            task_t t;
            while (q.steal_top(t)) t();
        });
    }

    // Owner competes with thieves.
    {
        task_t t;
        while (q.pop_bottom(t)) t();
    }

    thieves.clear(); // join

    ASSERT_EQ(counter.load(std::memory_order_relaxed), static_cast<int>(kTasks));
}
