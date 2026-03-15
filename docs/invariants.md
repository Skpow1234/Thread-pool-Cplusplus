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

### WorkStealingQueue (lock-free, M12)

| Invariant | Explanation |
|-----------|-------------|
| `capacity_` is always a power of two | Enforced by `std::bit_ceil` in the constructor. Enables bitwise-AND index wrapping. |
| Indices grow monotonically | `top_` and `bottom_` are `atomic<uint64_t>` and never reset. Wrapping is handled entirely by `& mask_`. |
| `bottom_ - top_ <= capacity_` always holds | The full-check in `push_bottom` enforces this. Overflow would corrupt the circular buffer. |
| `push_bottom` is side-effect-free on failure | If the buffer is full, the task is not moved and the caller retains ownership. |
| `pop_bottom` / `steal_top` are side-effect-free on empty | Returns `false`; `out` is not modified. |
| Uint64 underflow guard | `pop_bottom` pre-checks `bottom_ == top_` before the speculative decrement. Decrementing 0 would wrap to `UINT64_MAX`; the unsigned `t > b` check would then fail to detect empty. |
| CAS-before-move | Both `pop_bottom` and `steal_top` win exclusive ownership via CAS on `top_` *before* calling `std::move` on the buffer slot. This prevents two racing threads from both moving a `move_only_function`. |

### CentralizedQueue

| Invariant | Explanation |
|-----------|-------------|
| Unbounded | `push` always succeeds (backed by `std::queue` which grows on demand). |
| `try_pop` never blocks | Returns `false` immediately when empty; never waits. |
| FIFO order | Tasks are popped in the same order they were pushed. |

---

## 2. Chase-Lev Lock-Free Deque — Memory Ordering Analysis (M12)

Each atomic operation is annotated with its justification. The fundamental trade-off
is **correctness** (no data races, no lost/duplicated tasks) vs **cost** (seq_cst
fences are expensive; relaxed ops are cheap).

### push_bottom — owner only

```bash
(1) bottom_.load(relaxed)    — owner-private; no concurrent writer → no ordering needed.
(2) top_.load(acquire)       — syncs with any CAS that advanced top_ (from steal_top or
                               pop_bottom's own CAS).  Ensures we see the most recent
                               steal and don't incorrectly declare the buffer full.
(3) buffer_[b & mask_] = task — plain write; ordering provided by (4).
(4) bottom_.store(b+1, release) — publishes the buffer write.  Any thread that
                               acquire-loads bottom_ (e.g. steal_top step 3) is
                               guaranteed to see the task written in (3).
```

### pop_bottom — owner only

```bash
Pre-check: if bottom_ == top_.load(acquire) → return false immediately.
           Prevents uint64 underflow: decrement of 0 would wrap to UINT64_MAX.

(3) bottom_.store(b, relaxed)   — speculative decrement; ordering via seq_cst fence.
(4) atomic_thread_fence(seq_cst) — CRITICAL pairing point with steal_top's fence.
                               Establishes a total order between pop_bottom and
                               steal_top.  Two outcomes:
                               • Our fence before thief's: thief sees bottom_ = b.
                               • Thief's fence before ours: we see thief's top_ advance.
                               Either way the "last element" race is detected correctly.
(5) top_.load(relaxed)          — sufficient; seq_cst fence already synchronises.
(6) top_.CAS(t, t+1, seq_cst)   — participates in the same total order as steal_top's
                               CAS.  The CAS winner is the sole accessor of slot b.
                               CAS-before-move: buffer not touched until after winning.
```

### steal_top — any thread

```bash
(1) top_.load(acquire)         — syncs with any prior successful CAS on top_, ensuring
                               we don't re-steal an already-claimed slot.
(2) atomic_thread_fence(seq_cst) — pairs with pop_bottom's fence.  If owner decremented
                               bottom_ before this fence, we see bottom_ = b in (3)
                               and correctly treat the deque as empty or near-empty.
(3) bottom_.load(acquire)      — syncs with push_bottom's release store, making the
                               task written at buffer_[pushed_b & mask_] visible to us.
(4) top_.CAS(t, t+1, seq_cst)  — atomically claims slot t.  Only the CAS winner moves
                               from the buffer, preventing double-move of a task_t.
After CAS win: buffer_[t & mask_] exclusively ours; (3) acquire guaranteed visibility.
```

### Why seq_cst for the fences (not acq_rel)?

An `acq_rel` fence only establishes ordering relative to the thread's own operations.
It does not create a *total order* between two different threads' fences.  The
`seq_cst` fence ensures that if thread A fences before thread B, every seq_cst
operation in A is visible to B.  Without this, the last-element race in `pop_bottom`
could go undetected: both the owner and a thief might believe they own the last slot.

---

## 3. Cache-Line Layout (M9)

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

```bash
offset   0: capacity_, mask_, buffer_, mtx_     ← cold, read-mostly
offset  N: [padding to next cache line]
offset 64k: top_     (64 bytes, thief-hot)     ← one cache line
offset 64k+64: bottom_ (64 bytes, owner-hot)   ← separate cache line
```

This is the correct layout for the lock-free upgrade (M12) where both indices will
be `std::atomic` and written concurrently without a mutex.

---

## 4. Benchmark Baseline (M9)

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

---

## 5. Benchmark Delta — Lock-Free vs Locked (M12)

**Platform:** Windows 10, 32× 3400 MHz (AMD Ryzen Threadripper PRO, 16 physical cores)  
**Compiler:** MSVC 19.44 (VS 2022 BuildTools), `/O2 /DNDEBUG`  
**Build:** `cmake --preset release` + `cmake --build build/release --config Release`

### `BM_SubmitBatch` — external-thread submissions

Tasks submitted from a single external thread; all work goes through the global
`CentralizedQueue` (the lock-free deque's fast path is **not** exercised here).

| batch | workers | M9 locked | M12 lock-free | delta |
|------:|--------:|----------:|--------------:|------:|
| 1 000 | 1       | 0.334 ms  | 0.282 ms      | **+18 %** |
| 1 000 | 2       | 0.509 ms  | 0.376 ms      | **+35 %** |
| 1 000 | 4       | 0.934 ms  | 0.762 ms      | **+23 %** |
| 1 000 | 8       | 1.68 ms   | 1.85 ms       | −9 %      |
| 10 000| 4       | 9.52 ms   | 10.3 ms       | −8 %      |
| 10 000| 8       | 15.6 ms   | 25.3 ms       | −38 %     |

> **Observation:** At low batch × workers counts the lock-free deque wins because
> contention on the global overflow queue (which has a mutex and is unchanged) is
> lower — workers spend less time waiting and return to their idle loop faster.
> At high worker counts (8+) with large batches, the lock-free deque shows a
> regression: the `seq_cst` fence in `steal_top` is expensive on Windows (MSVC does
> not elide it), and with 8 workers all stealing from each other's now-empty deques
> the fence overhead exceeds the mutex savings.  This is the expected trade-off for
> external-submission workloads; the benefit is realised for **worker-submitted**
> tasks (recursive workloads), which are the primary target of work-stealing.

### `BM_SubmitLatency` — single-task round-trip

| workers | M9 locked | M12 lock-free | delta |
|--------:|----------:|--------------:|------:|
| 1       | 1.38 µs   | 0.634 µs      | **+54 %** |
| 2       | 1.43 µs   | 0.946 µs      | **+34 %** |
| 4       | 1.52 µs   | 1.06 µs       | **+30 %** |
| 8       | 2.44 µs   | 3.22 µs       | −24 %     |

> **Observation:** Single-task latency improves significantly for 1–4 workers.
> The fast path for the idle worker loop no longer acquires the deque mutex on
> every `pop_bottom` call.  With 8 workers the steal-fence overhead dominates.

### Summary

The lock-free Chase-Lev deque delivers measurable gains for low-to-moderate
worker counts (1–4) and small batches — the typical configuration for I/O-bound
or mixed workloads.  For CPU-bound workloads where workers submit their own
sub-tasks (bypassing the global queue entirely), the gains are expected to be
substantially larger because the owner's `push_bottom` / `pop_bottom` path
avoids all locks and uses only relaxed + release stores.

---

## 6. Future Baseline Updates

| Milestone | Expected change |
|-----------|----------------|
| M13 (fibonacci benchmark) | Demonstrates LIFO cache-locality benefit for recursive task trees submitted by workers |
