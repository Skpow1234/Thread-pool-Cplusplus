# cmake/clang-tidy.cmake
# Activated by: -DENABLE_CLANG_TIDY=ON
#
# Provides two integration points:
#   1. CMAKE_CXX_CLANG_TIDY — runs clang-tidy on every compiled TU automatically
#      (Ninja / Makefile generators only; Visual Studio is not supported by CMake).
#   2. 'tidy' custom target — runs clang-tidy directly on the production sources
#      using the implicit include paths CMake detected at configure time.
#      Works with any generator.  Run with:
#        cmake --build <dir> --target tidy
#
# Windows note:
#   scoop install llvm           # provides clang-tidy, clang++
#   scoop install ninja          # required for cmake --preset lint
#   The 'lint' preset uses Ninja + Clang for compiler detection; if clang++ cannot
#   find the MSVC CRT (linker phase), configure the project with --preset debug
#   first (MSVC), then run the 'tidy' target to lint the production sources.

option(ENABLE_CLANG_TIDY "Run clang-tidy on all targets" OFF)

if(NOT ENABLE_CLANG_TIDY)
    return()
endif()

find_program(CLANG_TIDY_EXE
    NAMES clang-tidy clang-tidy-22 clang-tidy-21 clang-tidy-18 clang-tidy-17)

if(NOT CLANG_TIDY_EXE)
    message(WARNING
        "[clang-tidy] ENABLE_CLANG_TIDY is ON but clang-tidy was not found. "
        "Install it with 'scoop install llvm' (Windows) or your system package manager.")
    return()
endif()

message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")

# ---------------------------------------------------------------------------
# 'tidy' custom target
#
# Lints the production source files (src/) using the implicit include dirs that
# CMake discovered when it probed the compiler.  This works with any generator
# and does not require a successful link step.
# ---------------------------------------------------------------------------

# Production sources to analyse.
set(_TIDY_SOURCES "${CMAKE_SOURCE_DIR}/src/thread_pool.cpp")

# Pass every compiler-implicit include directory as an extra argument so
# clang-tidy finds the C++ standard library headers regardless of which
# compiler/SDK is installed.
set(_TIDY_EXTRA_ARGS
    "--extra-arg=-std=c++23"
    "--extra-arg=-I${CMAKE_SOURCE_DIR}/include"
)

foreach(_inc IN LISTS CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
    # Normalise slashes; clang-tidy on Windows accepts forward slashes.
    cmake_path(CONVERT "${_inc}" TO_CMAKE_PATH_LIST _inc_cmake NORMALIZE)
    list(APPEND _TIDY_EXTRA_ARGS "--extra-arg=-I${_inc_cmake}")
endforeach()

add_custom_target(tidy
    COMMAND "${CLANG_TIDY_EXE}"
            ${_TIDY_EXTRA_ARGS}
            ${_TIDY_SOURCES}
            --
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "clang-tidy: checking production sources..."
    VERBATIM
)

# ---------------------------------------------------------------------------
# Automatic per-TU integration (Ninja / Makefile only)
# ---------------------------------------------------------------------------
if(NOT CMAKE_GENERATOR MATCHES "Visual Studio")
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}" CACHE STRING "" FORCE)
    message(STATUS
        "[clang-tidy] CMAKE_CXX_CLANG_TIDY set — "
        "clang-tidy will run on every compiled translation unit.")
else()
    message(STATUS
        "[clang-tidy] Visual Studio generator: CMAKE_CXX_CLANG_TIDY is not "
        "supported.  Use 'cmake --build <dir> --target tidy' instead.")
endif()
