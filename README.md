# Thread Pool — C++23

A lock-based thread pool implementation in modern C++23, built with correctness and performance in mind.

---

## Features

- C++23 standard throughout
- CMake build system
- Unit tests (GoogleTest or Catch2)
- Sanitizer builds: ASan, UBSan, TSan
- Static analysis via clang-tidy
- Benchmarks for hot paths
- Documented invariants

---

## Project Structure

```bash
Thread-pool-Cplusplus/
├── include/
│   └── thread_pool/
│       └── thread_pool.hpp   # Public API
├── src/
│   └── thread_pool.cpp       # Implementation
├── tests/
│   └── test_thread_pool.cpp  # Unit tests
├── benchmarks/
│   └── bench_thread_pool.cpp # Benchmarks
├── cmake/
│   ├── sanitizers.cmake      # ASan / UBSan / TSan presets
│   └── clang-tidy.cmake      # clang-tidy integration
├── docs/
│   └── invariants.md         # Documented invariants
├── CMakeLists.txt
└── .gitignore
```

---

## Building

### Standard build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Debug with sanitizers

```bash
# AddressSanitizer + UndefinedBehaviorSanitizer
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DSANITIZE=asan,ubsan
cmake --build build-asan

# ThreadSanitizer
cmake -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DSANITIZE=tsan
cmake --build build-tsan
```

### Run tests

```bash
ctest --test-dir build --output-on-failure
```

### Run clang-tidy

```bash
cmake -B build-tidy -DENABLE_CLANG_TIDY=ON
cmake --build build-tidy
```

### Run benchmarks

```bash
./build/benchmarks/bench_thread_pool
```

---
