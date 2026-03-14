#include <benchmark/benchmark.h>

// M1 — placeholder so the benchmark target compiles.
// Real benchmarks are added in M13.
static void BM_Placeholder(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(0);
    }
}
BENCHMARK(BM_Placeholder);
