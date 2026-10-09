# Strict warnings for our own targets (CLAUDE.md, plan/features/0001-m0-skeleton.md).
# Interface target hio::warnings - linked by every hio target, never by third-party targets.
include_guard(GLOBAL)

add_library(hio_warnings INTERFACE)
add_library(hio::warnings ALIAS hio_warnings)

set(_hio_clang_warnings
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Wshadow
    -Wold-style-cast
    -Wnon-virtual-dtor
    -Wimplicit-fallthrough
    -Wcast-qual
    -Wformat=2)

if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    # clang-cl: MSVC-style switches plus the clang warnings passed through /clang:.
    set(_hio_clang_cl_warnings /W4 /WX /permissive-)
    foreach(_flag IN LISTS _hio_clang_warnings)
        list(APPEND _hio_clang_cl_warnings "/clang:${_flag}")
    endforeach()
    target_compile_options(hio_warnings INTERFACE ${_hio_clang_cl_warnings})
else()
    target_compile_options(hio_warnings INTERFACE ${_hio_clang_warnings})
endif()

unset(_hio_clang_warnings)
unset(_hio_clang_cl_warnings)
