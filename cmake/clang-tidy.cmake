# cmake/clang-tidy.cmake
# Activated by: -DENABLE_CLANG_TIDY=ON
#
# Requires a .clang-tidy file at the repo root (added in M11).
# Requires clang-tidy to be on PATH.

option(ENABLE_CLANG_TIDY "Run clang-tidy on all targets" OFF)

if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy clang-tidy-18 clang-tidy-17)
    if(NOT CLANG_TIDY_EXE)
        message(WARNING "ENABLE_CLANG_TIDY is ON but clang-tidy was not found.")
    else()
        message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}" CACHE STRING "" FORCE)
    endif()
endif()
