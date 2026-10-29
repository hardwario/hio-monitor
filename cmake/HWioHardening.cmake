# Hardened standard library assertions (CLAUDE.md, plan/features/0001-m0-skeleton.md).
# Interface target hwio::hardening - linked by every hwio target.
#
#   Debug   : _GLIBCXX_ASSERTIONS (libstdc++), libc++ EXTENSIVE hardening,
#             MSVC STL debug iterators (_ITERATOR_DEBUG_LEVEL=2, the MSVC Debug default).
#   Release : _GLIBCXX_ASSERTIONS (libstdc++), libc++ FAST hardening (confirmed 2026-10-09).
#
# Each macro only affects its own standard library, so all of them can be defined everywhere.
#
# Compiler and linker hardening (ADR-0187, plan/features/0025 M-5), in builds without sanitizers
# and fuzzers (HWIO_SANITIZERS empty), on every own target and executable:
#   clang      : -fstack-protector-strong; -fstack-clash-protection (Linux); -fcf-protection=full
#                (x86-64; on macOS only for the x86_64 slice of a universal build);
#                -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3 (Release / RelWithDebInfo: needs optimisation)
#   Linux link : -z relro -z now -z noexecstack
#   clang-cl   : /guard:cf (compile and link), lld-link /CETCOMPAT /DYNAMICBASE /HIGHENTROPYVA
#                /NXCOMPAT
# The CTest binary_hardening_check (tests/cmake) verifies the built executables.
include_guard(GLOBAL)

add_library(hwio_hardening INTERFACE)
add_library(hwio::hardening ALIAS hwio_hardening)

set(_hwio_is_debug "$<CONFIG:Debug>")

target_compile_definitions(hwio_hardening INTERFACE
    _GLIBCXX_ASSERTIONS
    "$<IF:${_hwio_is_debug},_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE,_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST>")

if(NOT HWIO_SANITIZERS)
    set(_hwio_optimized "$<CONFIG:Release,RelWithDebInfo>")
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options(hwio_hardening INTERFACE /guard:cf)
        target_link_options(hwio_hardening INTERFACE
            /guard:cf /CETCOMPAT /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT)
    else()
        target_compile_options(hwio_hardening INTERFACE
            -fstack-protector-strong
            "$<${_hwio_optimized}:-U_FORTIFY_SOURCE>"
            "$<${_hwio_optimized}:-D_FORTIFY_SOURCE=3>")
        if(APPLE)
            if("x86_64" IN_LIST CMAKE_OSX_ARCHITECTURES OR
               (NOT CMAKE_OSX_ARCHITECTURES AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$"))
                target_compile_options(hwio_hardening INTERFACE
                    "SHELL:-Xarch_x86_64 -fcf-protection=full")
            endif()
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
            target_compile_options(hwio_hardening INTERFACE -fcf-protection=full)
        endif()
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_compile_options(hwio_hardening INTERFACE -fstack-clash-protection)
            target_link_options(hwio_hardening INTERFACE
                "LINKER:-z,relro" "LINKER:-z,now" "LINKER:-z,noexecstack")
        endif()
    endif()
    unset(_hwio_optimized)
endif()

unset(_hwio_is_debug)
