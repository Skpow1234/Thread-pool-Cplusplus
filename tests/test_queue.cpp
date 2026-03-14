#include <gtest/gtest.h>

#include "thread_pool/centralized_queue.hpp"

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
