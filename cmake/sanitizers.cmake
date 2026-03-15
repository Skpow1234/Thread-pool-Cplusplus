# cmake/sanitizers.cmake
# Provides: target_sanitizers(<target>)
#
# Activated by cache variables (set via CMakePresets.json or -D flags):
#   SANITIZE_ASAN_UBSAN  — AddressSanitizer + UndefinedBehaviorSanitizer
#   SANITIZE_TSAN        — ThreadSanitizer  (Linux/macOS only)
#
# Compiler support
# ────────────────
#   GCC / Clang  — full support for both presets.
#   MSVC         — neither sanitizer is supported; a warning is emitted and
#                  the function returns without adding any flags so the build
#                  still succeeds (without instrumentation).
#
# Mutual exclusion
# ────────────────
#   ASan/UBSan and TSan use incompatible runtimes; enabling both is an error.

option(SANITIZE_ASAN_UBSAN "Enable -fsanitize=address,undefined" OFF)
option(SANITIZE_TSAN        "Enable -fsanitize=thread"           OFF)

if(SANITIZE_ASAN_UBSAN AND SANITIZE_TSAN)
    message(FATAL_ERROR
        "SANITIZE_ASAN_UBSAN and SANITIZE_TSAN are mutually exclusive. "
        "Enable only one at a time.")
endif()

function(target_sanitizers target)
    if(NOT (SANITIZE_ASAN_UBSAN OR SANITIZE_TSAN))
        return()
    endif()

    # MSVC does not support -fsanitize. Emit a warning and skip rather than
    # producing a build error, so CI on Windows can still run the debug build.
    if(MSVC)
        message(WARNING
            "[sanitizers] MSVC detected — sanitizer flags are not supported. "
            "Use the *-clang presets with 'scoop install llvm ninja' to enable "
            "ASan/UBSan on Windows. TSan requires Linux or macOS.")
        return()
    endif()

    if(SANITIZE_ASAN_UBSAN)
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
            -fno-sanitize-recover=all      # treat all UB as fatal
        )
        target_link_options(${target} PRIVATE
            -fsanitize=address,undefined
        )
    endif()

    if(SANITIZE_TSAN)
        target_compile_options(${target} PRIVATE
            -fsanitize=thread
            -fno-omit-frame-pointer
        )
        target_link_options(${target} PRIVATE
            -fsanitize=thread
        )
    endif()
endfunction()
