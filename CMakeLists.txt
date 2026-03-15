cmake_minimum_required(VERSION 3.25)
project(ThreadPool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emit compile_commands.json for clang-tidy and IDEs.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(sanitizers)
include(clang-tidy)

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------
add_library(thread_pool STATIC src/thread_pool.cpp)

target_include_directories(thread_pool PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_options(thread_pool PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Werror>
    # /wd4324: "structure was padded due to alignment specifier" — intentional;
    # top_ and bottom_ are aligned to separate cache lines by design (M9).
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /wd4324>
)

target_sanitizers(thread_pool)

# ---------------------------------------------------------------------------
# Tests & Benchmarks
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
add_subdirectory(benchmarks)
