#include <gtest/gtest.h>

#include "thread_pool/thread_pool.hpp"

#include <thread>

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
