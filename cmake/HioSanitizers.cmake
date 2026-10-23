# Sanitizer and coverage instrumentation (plan/features/0001-m0-skeleton.md).
#
#   HIO_SANITIZERS : "" (default), "address;undefined" (linux-asan, macos-asan), "thread"
#                    (linux-tsan) or "fuzzer;address;undefined" (linux-fuzz, ADR-0152). Applied to
#                    every target incl. third-party code, so that sanitizer runtimes see all of our
#                    process code instrumented. "fuzzer" compiles everything with
#                    -fsanitize=fuzzer-no-link (coverage instrumentation without libFuzzer's main);
#                    the fuzz executables add -fsanitize=fuzzer through hio_add_fuzzer().
#   HIO_BUILD_FUZZERS requires "fuzzer" in HIO_SANITIZERS.
#   HIO_COVERAGE   : ON for linux-coverage (clang source-based coverage).
#
# Runtime options (TSAN_OPTIONS, UBSAN_OPTIONS, ...) are set by the test presets.
include_guard(GLOBAL)

set(HIO_SANITIZERS "" CACHE STRING
    "Sanitizers to enable: empty, 'address;undefined', 'thread' or 'fuzzer;address;undefined'")
option(HIO_COVERAGE "Instrument the build for clang source-based code coverage" OFF)

if(HIO_SANITIZERS)
    if("thread" IN_LIST HIO_SANITIZERS AND "address" IN_LIST HIO_SANITIZERS)
        message(FATAL_ERROR "HIO_SANITIZERS: 'thread' cannot be combined with 'address'.")
    endif()
    foreach(_san IN LISTS HIO_SANITIZERS)
        if(NOT _san MATCHES "^(address|undefined|thread|fuzzer)$")
            message(FATAL_ERROR "HIO_SANITIZERS: unsupported sanitizer '${_san}'.")
        endif()
    endforeach()
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        message(FATAL_ERROR "HIO_SANITIZERS is supported on Linux and macOS only.")
    endif()
    if("fuzzer" IN_LIST HIO_SANITIZERS AND "thread" IN_LIST HIO_SANITIZERS)
        message(FATAL_ERROR "HIO_SANITIZERS: 'fuzzer' cannot be combined with 'thread'.")
    endif()

    # Compile: fuzzer -> fuzzer-no-link. Link: everything except the fuzzer (only the fuzz
    # executables link libFuzzer, hio_add_fuzzer()).
    set(_hio_san_compile ${HIO_SANITIZERS})
    list(TRANSFORM _hio_san_compile REPLACE "^fuzzer$" "fuzzer-no-link")
    set(_hio_san_link ${HIO_SANITIZERS})
    list(REMOVE_ITEM _hio_san_link fuzzer)
    list(JOIN _hio_san_compile "," _hio_san_list)
    add_compile_options(
        "-fsanitize=${_hio_san_list}"
        -fno-sanitize-recover=all
        -fno-omit-frame-pointer
        -g)
    if(_hio_san_link)
        list(JOIN _hio_san_link "," _hio_san_link_list)
        add_link_options("-fsanitize=${_hio_san_link_list}")
    endif()
    message(STATUS "HARDWARIO Monitor: sanitizers enabled: ${_hio_san_list}")
    unset(_hio_san_compile)
    unset(_hio_san_link)
    unset(_hio_san_list)
    unset(_hio_san_link_list)
endif()

if(HIO_BUILD_FUZZERS AND NOT "fuzzer" IN_LIST HIO_SANITIZERS)
    message(FATAL_ERROR
        "HIO_BUILD_FUZZERS=ON needs HIO_SANITIZERS to contain 'fuzzer' (use the linux-fuzz preset, "
        "ADR-0152).")
endif()

# hio_add_fuzzer(<name> <source>...): a libFuzzer executable (ADR-0152), linked with
# -fsanitize=fuzzer (libFuzzer's main) in addition to the global sanitizers.
function(hio_add_fuzzer name)
    add_executable(${name} ${ARGN})
    target_compile_options(${name} PRIVATE -fsanitize=fuzzer)
    target_link_options(${name} PRIVATE -fsanitize=fuzzer)
endfunction()

if(HIO_COVERAGE)
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        message(FATAL_ERROR "HIO_COVERAGE is supported with the GNU-style clang driver only.")
    endif()
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
    add_link_options(-fprofile-instr-generate)
    message(STATUS "HARDWARIO Monitor: source-based coverage enabled")
endif()
