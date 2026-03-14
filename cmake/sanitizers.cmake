# cmake/sanitizers.cmake
# Provides: target_sanitizers(<target>)
#
# Activated by cache variables set in CMakePresets.json:
#   SANITIZE_ASAN_UBSAN  — AddressSanitizer + UndefinedBehaviorSanitizer
#   SANITIZE_TSAN        — ThreadSanitizer
#
# ASan/UBSan and TSan are mutually exclusive; CMake will error if both are ON.

option(SANITIZE_ASAN_UBSAN "Enable -fsanitize=address,undefined" OFF)
option(SANITIZE_TSAN        "Enable -fsanitize=thread"           OFF)

if(SANITIZE_ASAN_UBSAN AND SANITIZE_TSAN)
    message(FATAL_ERROR
        "SANITIZE_ASAN_UBSAN and SANITIZE_TSAN are mutually exclusive. "
        "Enable only one at a time.")
endif()

function(target_sanitizers target)
    if(SANITIZE_ASAN_UBSAN)
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
            -fno-sanitize-recover=all
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
