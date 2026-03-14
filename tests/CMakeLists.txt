include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE
    FIND_PACKAGE_ARGS NAMES GTest
)

# Don't install GTest alongside our project.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)
include(GoogleTest)

# ---------------------------------------------------------------------------
# Helper: create a gtest binary linked to thread_pool.
# Usage: add_gtest(<name> <sources...>)
# ---------------------------------------------------------------------------
function(add_gtest name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE thread_pool GTest::gtest_main)
    target_sanitizers(${name})
    gtest_discover_tests(${name})
endfunction()

# ---------------------------------------------------------------------------
# Test targets (one per milestone, added as we go)
# ---------------------------------------------------------------------------
add_gtest(test_thread_pool test_thread_pool.cpp)  # M1 smoke → grows each milestone
add_gtest(test_task        test_task.cpp)         # M2 task_t type alias
