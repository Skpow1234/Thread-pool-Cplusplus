#include <gtest/gtest.h>

#include "thread_pool/thread_pool.hpp"

#include <future>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <vector>

using tp::ThreadPool;

// ---------------------------------------------------------------------------
// M1 smoke test — kept as a compile-time sanity check.
// ---------------------------------------------------------------------------
TEST(Smoke, AlwaysPasses) {
    ASSERT_TRUE(true);
}

// ---------------------------------------------------------------------------
// M4: Worker thread lifecycle.
//
// Each test constructs a pool and lets it fall out of scope.
// The destructor must:
//   1. Request stop on every jthread (automatic).
//   2. Join every jthread (automatic).
// A hang here means workers are not honouring the stop_token.
// A crash here means the destructor accessed invalid state.
// ---------------------------------------------------------------------------

// Default constructor uses hardware_concurrency() threads.
TEST(ThreadPool, ConstructAndDestructDefault) {
    ThreadPool pool;
}

// A pool with zero workers is valid — tasks queue but are never executed.
TEST(ThreadPool, ConstructWithZeroThreads) {
    ThreadPool pool{0};
}

// Single-worker edge case.
TEST(ThreadPool, ConstructWithOneThread) {
    ThreadPool pool{1};
}

// Explicit hardware_concurrency().
TEST(ThreadPool, ConstructWithHardwareConcurrency) {
    ThreadPool pool{std::thread::hardware_concurrency()};
}

// Two pools can coexist and shut down independently.
TEST(ThreadPool, TwoPoolsCoexist) {
    ThreadPool a{2};
    ThreadPool b{2};
}

// ---------------------------------------------------------------------------
// M5: submit() returning std::future<T>.
// ---------------------------------------------------------------------------

// Submit a lambda returning int; future carries the result.
TEST(ThreadPool, SubmitReturnsValue) {
    ThreadPool pool{2};
    auto f = pool.submit([] { return 42; });
    ASSERT_EQ(f.get(), 42);
}

// Submit a void callable; future.get() completes without throwing.
TEST(ThreadPool, SubmitVoidCallable) {
    ThreadPool pool{2};
    bool ran = false;
    auto f = pool.submit([&ran] { ran = true; });
    f.get();
    ASSERT_TRUE(ran);
}

// Submit a move-only callable (captures unique_ptr).
// This would be a compile error with std::function-based pools.
TEST(ThreadPool, SubmitMoveOnlyCallable) {
    ThreadPool pool{2};
    auto ptr = std::make_unique<int>(99);
    int* raw = ptr.get();

    auto f = pool.submit([p = std::move(ptr)] { return *p; });
    ASSERT_EQ(f.get(), 99);
    // ptr is now empty; raw still points to the int that lived inside the task.
    (void)raw;
}

// Submit 1 000 tasks from one thread; collect all futures; verify every result.
TEST(ThreadPool, Submit1000Tasks) {
    ThreadPool pool{4};

    constexpr int kTasks = 1'000;
    std::vector<std::future<int>> futures;
    futures.reserve(kTasks);

    for (int i = 0; i < kTasks; ++i) {
        futures.push_back(pool.submit([i] { return i * i; }));
    }

    for (int i = 0; i < kTasks; ++i) {
        ASSERT_EQ(futures[i].get(), i * i);
    }
}

// Submit tasks from multiple threads simultaneously.
TEST(ThreadPool, MultiThreadedSubmit) {
    ThreadPool pool{4};

    constexpr int kSubmitters = 4;
    constexpr int kTasksEach  = 250;
    constexpr int kTotal      = kSubmitters * kTasksEach;

    std::vector<std::future<int>> futures(kTotal);

    {
        std::vector<std::jthread> submitters;
        submitters.reserve(kSubmitters);
        for (int t = 0; t < kSubmitters; ++t) {
            submitters.emplace_back([&, t] {
                for (int i = 0; i < kTasksEach; ++i) {
                    int idx = t * kTasksEach + i;
                    futures[idx] = pool.submit([idx] { return idx; });
                }
            });
        }
    } // submitters joined

    int sum = 0;
    for (auto& f : futures) sum += f.get();

    // Sum of 0..kTotal-1
    const int expected = kTotal * (kTotal - 1) / 2;
    ASSERT_EQ(sum, expected);
}

// ---------------------------------------------------------------------------
// M6: Exception propagation.
//
// std::packaged_task captures any exception thrown by the callable and stores
// it in the shared state. future.get() then rethrows it on the calling thread.
// The pool itself needs no changes — this comes for free.
// ---------------------------------------------------------------------------

// Standard library exception type propagates with the correct dynamic type.
TEST(ThreadPool, ExceptionRuntimeError) {
    ThreadPool pool{2};

    auto f = pool.submit([] -> int {
        throw std::runtime_error{"boom"};
    });

    ASSERT_THROW(f.get(), std::runtime_error);
}

// Derived exception type: only the base is caught, but the message is intact.
TEST(ThreadPool, ExceptionMessagePreserved) {
    ThreadPool pool{2};

    auto f = pool.submit([] -> int {
        throw std::runtime_error{"sentinel"};
    });

    try {
        f.get();
        FAIL() << "expected exception was not thrown";
    } catch (const std::runtime_error& e) {
        ASSERT_STREQ(e.what(), "sentinel");
    }
}

// Non-standard (non-exception) throw type still propagates via future.
// std::packaged_task stores it as std::exception_ptr, which re-throws correctly.
TEST(ThreadPool, ExceptionNonStandardType) {
    ThreadPool pool{2};

    auto f = pool.submit([] -> int { throw 42; });

    ASSERT_THROW(f.get(), int);
}

// After a throwing task the pool must remain fully operational.
TEST(ThreadPool, PoolOperationalAfterException) {
    ThreadPool pool{2};

    // Throw in the first task.
    auto bad = pool.submit([] -> int {
        throw std::runtime_error{"transient"};
    });
    ASSERT_THROW(bad.get(), std::runtime_error);

    // Pool must still execute subsequent tasks correctly.
    auto good = pool.submit([] { return 7; });
    ASSERT_EQ(good.get(), 7);
}

// Multiple tasks throw; all futures surface their individual exceptions;
// tasks submitted after are still executed.
TEST(ThreadPool, MultipleThrowingTasks) {
    ThreadPool pool{4};

    constexpr int kThrowing = 10;
    std::vector<std::future<int>> futures;
    futures.reserve(kThrowing);

    for (int i = 0; i < kThrowing; ++i) {
        futures.push_back(pool.submit([i] -> int {
            throw std::runtime_error{std::to_string(i)};
        }));
    }

    for (int i = 0; i < kThrowing; ++i) {
        try {
            futures[i].get();
            FAIL() << "expected exception for task " << i;
        } catch (const std::runtime_error& e) {
            ASSERT_EQ(std::stoi(e.what()), i);
        }
    }

    // Pool still works.
    ASSERT_EQ(pool.submit([] { return 99; }).get(), 99);
}
