# Thread Pool — Invariants & Benchmark Baseline

## 1. Runtime Invariants

### ThreadPool

| Invariant | Explanation |
|-----------|-------------|
| Worker count is fixed at construction | `workers_.size()` never changes after the constructor returns. |
| Workers outlive their deques | `workers_` is declared last in the class → destroyed first. All `jthread`s stop and join before `worker_queues_` is freed. |
| `local_queue_` is null for non-workers | Any thread that did not start as a pool worker has `local_queue_ == nullptr`. `submit()` uses this to choose the slow path. |
| Unstarted tasks are abandoned on destruction | Tasks queued but not yet picked up when the pool destructs will never execute. Their `std::future`s throw `std::future_error(broken_promise)` on `get()`. |
| Zero-worker pool is valid | A `ThreadPool{0}` accepts submitted tasks into the global queue; they will never execute. Useful for testing submission logic in isolation. |

### WorkStealingQueue

| Invariant | Explanation |
|-----------|-------------|
| `capacity_` is always a power of two | Enforced by `std::bit_ceil` in the constructor. Enables bitwise-AND index wrapping. |
| Indices grow monotonically | `top_` and `bottom_` are `uint64_t` and never reset. Wrapping is handled entirely by `& mask_`. |
| `bottom_ - top_ <= capacity_` always holds | The full-check in `push_bottom` enforces this. Overflow would corrupt the circular buffer. |
| `push_bottom` is side-effect-free on failure | If the buffer is full, the task is not moved and the caller retains ownership. |
| `pop_bottom` / `steal_top` are side-effect-free on empty | Returns `false`; `out` is not modified. |

### CentralizedQueue

| Invariant | Explanation |
|-----------|-------------|
| Unbounded | `push` always succeeds (backed by `std::queue` which grows on demand). |
| `try_pop` never blocks | Returns `false` immediately when empty; never waits. |
| FIFO order | Tasks are popped in the same order they were pushed. |

---

## 2. Cache-Line Layout (M9)

### The Problem: False Sharing

In `WorkStealingQueue`, `top_` is written by thief threads (via `steal_top`) and
`bottom_` is written by the owner thread (via `push_bottom` / `pop_bottom`).

If both variables share a 64-byte cache line, every write to `bottom_` by the owner
forces every thief's CPU core to invalidate its cached copy of that line — even though
the thieves only care about `top_`. This "false sharing" causes costly cross-core cache
coherency traffic on every single push and pop operation.

### The Fix

Both indices are annotated with `alignas(std::hardware_destructive_interference_size)`.
The compiler inserts padding between them so each lands on its own 64-byte cache line:

```
offset   0: capacity_, mask_, buffer_, mtx_     ← cold, read-mostly
offset  N: [padding to next cache line]
offset 64k: top_     (64 bytes, thief-hot)     ← one cache line
offset 64k+64: bottom_ (64 bytes, owner-hot)   ← separate cache line
```

This is the correct layout for the lock-free upgrade (M12) where both indices will
be `std::atomic` and written concurrently without a mutex.

---

## 3. Benchmark Baseline (M9)

**Platform:** Windows 10, 32× 3400 MHz (AMD Ryzen Threadripper PRO, 16 physical cores)  
**Compiler:** MSVC 19.44 (VS 2022 BuildTools), `/O2 /DNDEBUG`  
**Build:** `cmake --preset release` + `cmake --build build/release --config Release`  
**Binary:** `build/release/benchmarks/Release/bench_submit.exe`

### Batch Throughput — `BM_SubmitBatch`

Submit N trivial tasks and wait for all futures. Measures end-to-end throughput
(submit → schedule → execute → `future.get()`).

| batch | workers | wall time | tasks / second |
|------:|--------:|----------:|---------------:|
| 1 000 | 1       | 0.334 ms  | 3.14 M/s       |
| 1 000 | 2       | 0.509 ms  | 2.05 M/s       |
| 1 000 | 4       | 0.934 ms  | 1.11 M/s       |
| 1 000 | 8       | 1.68 ms   | 623 k/s        |
| 10 000| 4       | 9.52 ms   | 1.07 M/s       |
| 10 000| 8       | 15.6 ms   | 691 k/s        |

> **Observation:** Throughput decreases as worker count increases. This is expected for
> trivial tasks where the task body (a single `return i`) is far shorter than the
> scheduling overhead per task. More workers means more mutex contention on the global
> `CentralizedQueue` and more threads competing for the same work. The work-stealing
> fast path (local deque) is not exercised here because all tasks are submitted
> externally and go to the global overflow queue.

### Single-Task Latency — `BM_SubmitLatency`

Submit one task; wait for result; repeat. Measures round-trip latency dominated by
mutex acquisition, thread wake-up, and `std::future` overhead.

| workers | latency (wall) | throughput |
|--------:|---------------:|-----------:|
| 1       | 1.38 µs        | 755 k/s    |
| 2       | 1.43 µs        | 763 k/s    |
| 4       | 1.52 µs        | 703 k/s    |
| 8       | 2.44 µs        | 434 k/s    |

> **Observation:** Latency is roughly 1–2.5 µs per task. With more workers, latency
> rises due to contention on the global queue and OS scheduling overhead.
> The minimum latency (~1.3 µs) reflects the irreducible cost of a `packaged_task`
> wrap + one mutex lock/unlock + `yield`-based scheduling.

### Expected Improvement at M12 (Lock-Free Deque)

The lock-free Chase-Lev deque (M12) eliminates the mutex on the owner's push/pop path.
For workloads where workers spawn sub-tasks (recursive algorithms, task graphs), the
fast path avoids all global synchronisation. Expected improvement for such workloads:
2–10× depending on task granularity and core count.

---

## 4. Future Baseline Updates

| Milestone | Expected change |
|-----------|----------------|
| M12 (lock-free deque) | Batch throughput ↑ for recursive workloads; latency ↓ for worker-submitted tasks |
| M13 (fibonacci benchmark) | Demonstrates LIFO cache-locality benefit for recursive task trees |
