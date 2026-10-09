# Sanitizer and coverage instrumentation (plan/features/0001-m0-skeleton.md).
#
#   HIO_SANITIZERS : "" (default), "address;undefined" (linux-asan, macos-asan) or "thread"
#                    (linux-tsan). Applied to every target incl. third-party code, so that
#                    sanitizer runtimes see all of our process code instrumented.
#   HIO_COVERAGE   : ON for linux-coverage (clang source-based coverage).
#
# Runtime options (TSAN_OPTIONS, UBSAN_OPTIONS, ...) are set by the test presets.
include_guard(GLOBAL)

set(HIO_SANITIZERS "" CACHE STRING "Sanitizers to enable: empty, 'address;undefined' or 'thread'")
option(HIO_COVERAGE "Instrument the build for clang source-based code coverage" OFF)

if(HIO_SANITIZERS)
    if("thread" IN_LIST HIO_SANITIZERS AND "address" IN_LIST HIO_SANITIZERS)
        message(FATAL_ERROR "HIO_SANITIZERS: 'thread' cannot be combined with 'address'.")
    endif()
    foreach(_san IN LISTS HIO_SANITIZERS)
        if(NOT _san MATCHES "^(address|undefined|thread)$")
            message(FATAL_ERROR "HIO_SANITIZERS: unsupported sanitizer '${_san}'.")
        endif()
    endforeach()
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        message(FATAL_ERROR "HIO_SANITIZERS is supported on Linux and macOS only.")
    endif()

    list(JOIN HIO_SANITIZERS "," _hio_san_list)
    add_compile_options(
        "-fsanitize=${_hio_san_list}"
        -fno-sanitize-recover=all
        -fno-omit-frame-pointer
        -g)
    add_link_options("-fsanitize=${_hio_san_list}")
    message(STATUS "HARDWARIO Monitor: sanitizers enabled: ${_hio_san_list}")
    unset(_hio_san_list)
endif()

if(HIO_COVERAGE)
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        message(FATAL_ERROR "HIO_COVERAGE is supported with the GNU-style clang driver only.")
    endif()
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
    add_link_options(-fprofile-instr-generate)
    message(STATUS "HARDWARIO Monitor: source-based coverage enabled")
endif()
