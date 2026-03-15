#include <benchmark/benchmark.h>

#include "thread_pool/thread_pool.hpp"

#include <cstddef>
#include <future>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Fibonacci benchmark suite
//
// Goal: measure the pool's throughput and parallel efficiency on a real
// CPU-bound workload and demonstrate how the work-stealing deque distributes
// independent tasks.
//
// ── Why NOT recursive parallel Fibonacci? ────────────────────────────────────
// The textbook "spawn left, compute right" recursion has a well-known
// pitfall with fixed-size thread pools that use blocking futures:
//
//   A pool worker calls future::get() on a sub-task that it just pushed to
//   its OWN local deque.  If all workers reach this state simultaneously,
//   every pending sub-task is trapped in some blocked worker's local deque
//   and no free worker remains to steal and drain it.  Result: deadlock.
//
// This can only be avoided with:
//   (a) Continuation-stealing (a planned future milestone), or
//   (b) "Helping" — waiting workers steal and execute tasks while spinning on
//       future::get() — which our pool does not yet support, or
//   (c) Ensuring the spawning thread is NEVER a pool worker (e.g. only the
//       main thread drives the recursion), which limits exposed parallelism.
//
// ── Design choice ────────────────────────────────────────────────────────────
// BM_FibBatch submits N INDEPENDENT seq_fib(kFibN) tasks to the pool.
// Workers only compute — they never spawn sub-tasks or wait on futures.
// This is deadlock-free by construction and cleanly measures:
//
//   • The pool's ability to distribute CPU-bound work across cores.
//   • Work-stealing's effectiveness at keeping all workers busy when N ≫ W
//     (tasks/worker >> 1) and when task durations are uneven.
//   • The overhead of submit() + future::get() round-trips at scale.
//
// Expected wall time (ideal):
//   sequential  ≈ N × seq_fib(kFibN)
//   parallel/W  ≈ ⌈N/W⌉ × seq_fib(kFibN) + scheduling overhead
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// fib(30) ≈ 3 ms on this machine — large enough to dwarf scheduling overhead,
// small enough for many iterations per benchmark run.
constexpr int kFibN = 30;

// Fixed number of tasks in every batch.  Keeping this constant means the
// "total work" is identical across all configurations; only the number of
// workers varies.  A W-worker pool should finish in ≈ N/W × seq_fib(kFibN).
constexpr int kTotalTasks = 16;

// Sequential Fibonacci — no allocation, no threads, compiler-opaque via DoNotOptimize.
int seq_fib(int n) noexcept {
    if (n <= 1) { return n; }
    return seq_fib(n - 1) + seq_fib(n - 2);
}

} // namespace

// ── BM_FibSequential ─────────────────────────────────────────────────────────
// Single-threaded baseline.  Computes seq_fib(kFibN) once per iteration.
// Establishes the reference latency that the batch benchmark must beat.
static void BM_FibSequential(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(seq_fib(kFibN));
    }
    state.SetLabel("single core");
}
BENCHMARK(BM_FibSequential)->Unit(benchmark::kMillisecond);

// ── BM_FibBatch ──────────────────────────────────────────────────────────────
// Submit `workers × kTasksPerWorker` independent seq_fib(kFibN) computations
// to the pool and wait for all futures.
//
// With W workers and N = W × kTasksPerWorker tasks:
//   Ideal wall time = kTasksPerWorker × seq_fib(kFibN) / 1   (sequential ref)
//                   = kTasksPerWorker × seq_fib(kFibN)        (single worker)
//                   ≈ kTasksPerWorker × seq_fib(kFibN) / W   (W workers, ideal)
//
// Work-stealing keeps all workers busy even when one task finishes early,
// because idle workers steal pending tasks from overloaded peers' local deques.
//
// Expected wall time (ideal):
//   1 worker  ≈ kTotalTasks × seq_fib(kFibN)             (all tasks sequential)
//   W workers ≈ ⌈kTotalTasks / W⌉ × seq_fib(kFibN)       (near-linear speedup)
//
// Arg: number of pool workers.
static void BM_FibBatch(benchmark::State& state) {
    const auto workers = static_cast<std::size_t>(state.range(0));
    constexpr int  n_tasks = kTotalTasks;

    tp::ThreadPool pool{workers};

    for (auto _ : state) {
        std::vector<std::future<int>> futures;
        futures.reserve(static_cast<std::size_t>(n_tasks));

        for (int i = 0; i < n_tasks; ++i) {
            futures.push_back(pool.submit([] { return seq_fib(kFibN); }));
        }

        int total = 0;
        for (auto& f : futures) { total += f.get(); }
        benchmark::DoNotOptimize(total);
    }

    // Report both wall-clock items/sec and total tasks processed.
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(n_tasks));
}
BENCHMARK(BM_FibBatch)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Unit(benchmark::kMillisecond);

// ── BM_FibBatchJson ──────────────────────────────────────────────────────────
// Smoke test that --benchmark_format=json produces valid output.
// Runs the single-worker case once with a short iteration limit.
static void BM_FibBatchJsonSmoke(benchmark::State& state) {
    tp::ThreadPool pool{1};
    for (auto _ : state) {
        auto f = pool.submit([] { return seq_fib(10); });
        benchmark::DoNotOptimize(f.get());
    }
}
BENCHMARK(BM_FibBatchJsonSmoke)->Iterations(1)->Unit(benchmark::kMicrosecond);
