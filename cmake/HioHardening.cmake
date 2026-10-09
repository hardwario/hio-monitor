# Hardened standard library assertions (CLAUDE.md, plan/features/0001-m0-skeleton.md).
# Interface target hio::hardening - linked by every hio target.
#
#   Debug   : _GLIBCXX_ASSERTIONS (libstdc++), libc++ EXTENSIVE hardening,
#             MSVC STL debug iterators (_ITERATOR_DEBUG_LEVEL=2, the MSVC Debug default).
#   Release : _GLIBCXX_ASSERTIONS (libstdc++), libc++ FAST hardening (confirmed 2026-10-09).
#
# Each macro only affects its own standard library, so all of them can be defined everywhere.
include_guard(GLOBAL)

add_library(hio_hardening INTERFACE)
add_library(hio::hardening ALIAS hio_hardening)

set(_hio_is_debug "$<CONFIG:Debug>")

target_compile_definitions(hio_hardening INTERFACE
    _GLIBCXX_ASSERTIONS
    "$<IF:${_hio_is_debug},_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE,_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST>")

unset(_hio_is_debug)
