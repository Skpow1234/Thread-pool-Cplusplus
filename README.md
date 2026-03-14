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

## Project Structure

```bash
Thread-pool-Cplusplus/
├── include/thread_pool/
│   └── thread_pool.hpp       # Public API
├── src/
│   └── thread_pool.cpp       # Implementation
├── tests/
│   └── test_thread_pool.cpp  # Unit tests
├── benchmarks/
│   └── bench_thread_pool.cpp # Benchmarks
├── cmake/
│   ├── sanitizers.cmake      # ASan / UBSan / TSan helpers
│   └── clang-tidy.cmake      # clang-tidy integration
├── docs/
│   ├── DESIGN.md             # Architecture & milestones
│   └── invariants.md         # Runtime invariants
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

### Debug + ASan + UBSan *(Clang/GCC only)*

```bash
cmake --preset asan-ubsan
cmake --build build/asan-ubsan
```

### Debug + TSan *(Linux/macOS, Clang/GCC only)*

```bash
cmake --preset tsan
cmake --build build/tsan
```

### With clang-tidy *(requires `llvm` installed)*

```bash
cmake --preset debug -DENABLE_CLANG_TIDY=ON
cmake --build build/debug
```

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
# Windows (MSVC)
./build/debug/benchmarks/Debug/bench_thread_pool.exe

# Linux / macOS
./build/debug/benchmarks/bench_thread_pool
```

---

## Requirements

| Tool | Minimum version |
|------|----------------|
| CMake | 4.0 |
| MSVC | 19.44+ (VS 2022 BuildTools) |
| Clang | 17+ (for sanitizer presets) |
| GCC | 13+ |

---
