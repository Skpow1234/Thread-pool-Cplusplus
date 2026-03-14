#pragma once

#include <functional>

namespace tp {

// task_t is the canonical type-erased callable stored by the pool.
//
// std::move_only_function<void()> (C++23) is chosen over std::function<void()>
// because:
//   - it accepts move-only callables (e.g. lambdas that capture unique_ptr)
//   - it forbids accidental copies, making ownership explicit
//   - it avoids the small-buffer-overflow copies that std::function can trigger
//
// Invariant: a default-constructed or moved-from task_t is empty (operator bool
// returns false). The pool must never invoke an empty task_t.
using task_t = std::move_only_function<void()>;

} // namespace tp
