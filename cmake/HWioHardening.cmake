# Hardened standard library assertions (CLAUDE.md, plan/features/0001-m0-skeleton.md).
# Interface target hwio::hardening - linked by every hwio target.
#
#   Debug   : _GLIBCXX_ASSERTIONS (libstdc++), libc++ EXTENSIVE hardening,
#             MSVC STL debug iterators (_ITERATOR_DEBUG_LEVEL=2, the MSVC Debug default).
#   Release : _GLIBCXX_ASSERTIONS (libstdc++), libc++ FAST hardening (confirmed 2026-10-09).
#
# Each macro only affects its own standard library, so all of them can be defined everywhere.
include_guard(GLOBAL)

add_library(hwio_hardening INTERFACE)
add_library(hwio::hardening ALIAS hwio_hardening)

set(_hwio_is_debug "$<CONFIG:Debug>")

target_compile_definitions(hwio_hardening INTERFACE
    _GLIBCXX_ASSERTIONS
    "$<IF:${_hwio_is_debug},_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE,_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST>")

unset(_hwio_is_debug)
