#include <gtest/gtest.h>

#include "thread_pool/task.hpp"

#include <memory>
#include <type_traits>

using tp::task_t;

// task_t must be move-only: accepting move-only callables is the whole point.
static_assert(!std::is_copy_constructible_v<task_t>,
"task_t must not be copy-constructible");
static_assert(std::is_move_constructible_v<task_t>,
"task_t must be move-constructible");

// ---------------------------------------------------------------------------
// Store and invoke a plain lambda.
// ---------------------------------------------------------------------------
TEST(TaskT, InvokeLambda) {
    bool called = false;

    task_t t = [&called] { called = true; };
    t();

    ASSERT_TRUE(called);
}

// ---------------------------------------------------------------------------
// Store and invoke a move-only callable (captures a unique_ptr).
// This would be a compile error with std::function.
// ---------------------------------------------------------------------------
TEST(TaskT, InvokeMoveOnlyCallable) {
    auto ptr = std::make_unique<int>(42);
    int* raw = ptr.get();

    task_t t = [p = std::move(ptr)]() mutable { *p = 99; };
    t();

    ASSERT_EQ(*raw, 99);
}

// ---------------------------------------------------------------------------
// Moving a task_t transfers ownership; the source becomes empty.
// ---------------------------------------------------------------------------
TEST(TaskT, MoveTransfersOwnership) {
    bool called = false;

    task_t a = [&called] { called = true; };
    task_t b = std::move(a);

    // Moved-from task_t must be empty (operator bool returns false).
    ASSERT_FALSE(a);
    ASSERT_TRUE(b);

    b();
    ASSERT_TRUE(called);
}

// ---------------------------------------------------------------------------
// A default-constructed task_t is empty.
// ---------------------------------------------------------------------------
TEST(TaskT, DefaultConstructedIsEmpty) {
    task_t t;
    ASSERT_FALSE(t);
}
