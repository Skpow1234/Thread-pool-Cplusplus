include(FetchContent)

FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.9.1
    GIT_SHALLOW    TRUE
    FIND_PACKAGE_ARGS NAMES benchmark
)

# Disable benchmark's own tests and install rules.
set(BENCHMARK_ENABLE_TESTING   OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL   OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(benchmark)

# ---------------------------------------------------------------------------
# Helper: create a benchmark binary linked to thread_pool.
# Usage: add_benchmark(<name> <sources...>)
# ---------------------------------------------------------------------------
function(add_benchmark name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE thread_pool benchmark::benchmark_main)
    target_sanitizers(${name})
endfunction()

# ---------------------------------------------------------------------------
# Benchmark targets (populated in M13)
# ---------------------------------------------------------------------------
add_benchmark(bench_thread_pool bench_thread_pool.cpp)  # placeholder
