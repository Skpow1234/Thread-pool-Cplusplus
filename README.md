# Thread Pool — C++23

A work-stealing thread pool in modern C++23, built with correctness and performance in mind.

---

## Features

- C++23 standard throughout
- CMake 4.x build system with presets
- Unit tests via GoogleTest (auto-fetched)
- Benchmarks via Google Benchmark (auto-fetched)
- Sanitizer builds: ASan, UBSan, TSan (Clang/GCC on Linux/macOS)
- Static analysis via clang-tidy
- Documented invariants

---

## Architecture

The pool uses a **two-level work distribution** strategy:

```
External submit()
      │
      ▼
┌─────────────────────┐     pop / steal
│  CentralizedQueue   │ ──────────────────► worker threads
│  (global, MPMC)     │                         │
└─────────────────────┘               ┌──────────┴──────────┐
                                      │  WorkStealingQueue  │  ◄── push (fast path)
                                      │  per-worker, LIFO   │
                                      └─────────────────────┘
```

**Worker loop priority** (highest → lowest):
1. `pop_bottom` from own local deque (LIFO — owner gets its most-recently-pushed task, maximising cache reuse)
2. `try_pop` from the global `CentralizedQueue`
3. `steal_top` from a random peer's deque (FIFO end — leaves newer tasks for the owner)
4. `std::this_thread::yield()` and retry

**Key design decisions:**

| Decision | Rationale |
|----------|-----------|
| `std::jthread` + `std::stop_token` | Cooperative cancellation; joins automatically on destruction |
| `std::move_only_function<void()>` | Accepts move-only callables; avoids the copy-overhead of `std::function` |
| `std::packaged_task` + `std::future` | Type-erased return value; composable with `std::when_all` etc. |
| Lock-free Chase-Lev deque (M12) | Owner push/pop path acquires no mutex; thieves use CAS |
| Fixed thread count | Simpler lifetime model; avoids thundering-herd on resize |
| Active polling (`yield`) | Lower wake-up latency than condition-variable sleep for CPU-bound pools |

---

## Project Structure

```bash
Thread-pool-Cplusplus/
├── include/thread_pool/
│   ├── thread_pool.hpp          # Public API (ThreadPool)
│   ├── task.hpp                 # task_t alias (move_only_function<void()>)
│   ├── centralized_queue.hpp    # Global overflow queue (MPMC, mutex-protected)
│   └── work_stealing_queue.hpp  # Per-worker Chase-Lev deque (lock-free, M12)
├── src/
│   └── thread_pool.cpp          # ThreadPool implementation
├── tests/
│   ├── test_thread_pool.cpp     # ThreadPool unit tests
│   ├── test_queue.cpp           # Queue unit & stress tests
│   └── test_task.cpp            # task_t type tests
├── benchmarks/
│   ├── bench_submit.cpp         # Submit throughput & round-trip latency (M9)
│   ├── bench_fibonacci.cpp      # Parallel Fibonacci batch speedup (M13)
│   └── bench_thread_pool.cpp    # Placeholder for future benchmarks
├── cmake/
│   ├── sanitizers.cmake         # ASan / UBSan / TSan helpers
│   └── clang-tidy.cmake         # clang-tidy integration (custom tidy target)
├── docs/
│   ├── DESIGN.md                # Architecture & milestone roadmap
│   └── invariants.md            # Runtime invariants & benchmark baseline
├── CMakeLists.txt
├── CMakePresets.json
└── .gitignore
```

---

## Prerequisites

### Windows (recommended: Scoop)

Install [Scoop](https://scoop.sh) if you don't have it, then:

```powershell
scoop install cmake llvm ninja
```

| Package | Provides |
|---------|----------|
| `cmake` | Build system (4.x) |
| `llvm`  | `clang++`, `clang-tidy`, ASan/UBSan runtime |
| `ninja` | Fast build backend; required for `compile_commands.json` |

> **Note:** ThreadSanitizer (TSan) is not supported on Windows by any compiler.
> The `tsan` preset is intended for Linux/macOS CI. All other presets work on Windows.

Verify the tools are on your PATH:

```powershell
cmake --version     # 4.x
clang++ --version   # 21.x
ninja --version
```

### Linux / macOS

```bash
# Debian/Ubuntu
sudo apt install cmake ninja-build clang clang-tidy

# macOS (Homebrew)
brew install cmake ninja llvm
```

---

## Building

All build commands use **CMake presets** defined in `CMakePresets.json`.

### Debug (default development build)

```bash
cmake --preset debug
cmake --build build/debug
```

### Release

```bash
cmake --preset release
cmake --build build/release
```

### Sanitizer builds

> **Sanitizer support matrix**
>
> | Preset | Compiler needed | Platform |
> |--------|----------------|----------|
> | `asan-ubsan-clang` | Clang (`scoop install llvm ninja`) | Windows, Linux, macOS |
> | `tsan-clang` | Clang | Linux, macOS only |
> | `asan-ubsan` | Clang or GCC (uses default compiler) | Linux, macOS |
> | `tsan` | Clang or GCC (uses default compiler) | Linux, macOS |
>
> On **Windows + MSVC** the sanitizer presets build cleanly but emit a warning and
> skip the `-fsanitize=` flags — tests still pass, just without instrumentation.
> Install Clang via Scoop and use the `-clang` variants for real coverage.

```bash
# ASan + UBSan (Clang — works on Windows after scoop install llvm ninja)
cmake --preset asan-ubsan-clang
cmake --build build/asan-ubsan-clang
ctest --preset asan-ubsan-clang

# TSan (Clang — Linux/macOS only)
cmake --preset tsan-clang
cmake --build build/tsan-clang
ctest --preset tsan-clang
```

### With clang-tidy *(requires `llvm` installed)*

The `lint` preset configures the project with `ENABLE_CLANG_TIDY=ON`.
A `tidy` custom target then runs clang-tidy on the production sources
(`src/thread_pool.cpp` and all project headers it includes) using the
include paths that CMake detected at configure time.

```bash
# Configure once:
cmake --preset lint

# Check production sources (zero warnings required):
cmake --build build/lint --target tidy

# Full build + tests (optional):
cmake --build build/lint
ctest --test-dir build/lint --output-on-failure
```

> **Note (Windows):** `scoop install llvm` is the only prerequisite.
> The `lint` preset uses the default MSVC + Visual Studio generator,
> so no Developer Command Prompt is required.

---

## Running Tests

```bash
# Run all tests (debug preset)
ctest --preset debug

# Run with output on failure
ctest --preset debug --output-on-failure

# Run a specific test by name
./build/debug/tests/Debug/test_thread_pool.exe --gtest_filter="Smoke*"
```

Rebuild and test in one line:

```bash
cmake --build build/debug && ctest --preset debug --output-on-failure
```

---

## Running Benchmarks

```bash
# Windows (MSVC or Clang) — Release build recommended for meaningful numbers
cmake --preset release
cmake --build build/release --config Release

./build/release/benchmarks/Release/bench_submit.exe       # submit throughput & latency
./build/release/benchmarks/Release/bench_fibonacci.exe    # parallel Fibonacci speedup

# JSON output (for scripting / CI)
./build/release/benchmarks/Release/bench_submit.exe --benchmark_format=json

# Linux / macOS
./build/release/benchmarks/bench_submit
./build/release/benchmarks/bench_fibonacci
```

### Benchmark summary (MSVC Release, 32-core Threadripper PRO)

**`bench_submit` — external submit throughput** (`BM_SubmitBatch/1000`)

| workers | wall time | throughput |
|--------:|----------:|-----------:|
| 1       | 0.282 ms  | ~3.5 M/s   |
| 2       | 0.376 ms  | ~2.7 M/s   |
| 4       | 0.762 ms  | ~1.3 M/s   |
| 8       | 1.85 ms   | ~0.5 M/s   |

> Throughput falls with more workers because all external tasks go through the global
> `CentralizedQueue` (one mutex).  For worker-submitted tasks the fast lock-free path
> is used and contention vanishes.

**`bench_fibonacci` — 16 independent `seq_fib(30)` tasks** (≈ 3 ms each)

| workers | wall time | speedup |
|--------:|----------:|--------:|
| 1       | 51.7 ms   | 1.0×    |
| 2       | 32.4 ms   | 1.6×    |
| 4       | 16.0 ms   | 3.2×    |
| 8       |  8.83 ms  | 5.9×    |

> Near-linear parallel speedup for CPU-bound workloads.

See `docs/invariants.md` for the full baseline and lock-free delta.

---

## Requirements

| Tool | Minimum version |
|------|----------------|
| CMake | 4.0 |
| MSVC | 19.44+ (VS 2022 BuildTools) |
| Clang | 17+ (for sanitizer / clang-tidy presets) |
| GCC | 13+ |

---
