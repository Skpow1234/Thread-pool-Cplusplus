#include <benchmark/benchmark.h>

#include "thread_pool/thread_pool.hpp"

#include <future>
#include <vector>

// ---------------------------------------------------------------------------
// BM_SubmitBatch
//
// Submit a batch of N trivial tasks and wait for all to complete.
// Measures sustained throughput: how many tasks/sec the pool can process
// end-to-end (submit → execute → future.get()) at different concurrency levels.
//
// Args: {batch_size, num_workers}
// ---------------------------------------------------------------------------
static void BM_SubmitBatch(benchmark::State& state) {
    const auto batch   = static_cast<int>(state.range(0));
    const auto workers = static_cast<std::size_t>(state.range(1));

    tp::ThreadPool pool{workers};
    std::vector<std::future<int>> futures;
    futures.reserve(static_cast<std::size_t>(batch));

    for (auto _ : state) {
        futures.clear();
        for (int i = 0; i < batch; ++i) {
            futures.push_back(pool.submit([i] { return i; }));
        }
        for (auto& f : futures) {
            benchmark::DoNotOptimize(f.get());
        }
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(batch));
    state.SetLabel("tasks/iter=" + std::to_string(batch));
}

BENCHMARK(BM_SubmitBatch)
    ->Args({1'000, 1})
    ->Args({1'000, 2})
    ->Args({1'000, 4})
    ->Args({1'000, 8})
    ->Args({10'000, 4})
    ->Args({10'000, 8})
    ->Unit(benchmark::kMillisecond);

// ---------------------------------------------------------------------------
// BM_SubmitLatency
//
// Submit a single task and wait for its result. Repeat.
// Measures single-task round-trip latency: submit → schedule → execute → get.
// This is dominated by mutex acquisition, thread wake-up, and future overhead.
//
// Arg: num_workers
// ---------------------------------------------------------------------------
static void BM_SubmitLatency(benchmark::State& state) {
    const auto workers = static_cast<std::size_t>(state.range(0));
    tp::ThreadPool pool{workers};

    for (auto _ : state) {
        auto f = pool.submit([] { return 1; });
        benchmark::DoNotOptimize(f.get());
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(sizeof(int)));
    state.SetLabel("workers=" + std::to_string(workers));
}

BENCHMARK(BM_SubmitLatency)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Unit(benchmark::kMicrosecond);
