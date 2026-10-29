# Strict warnings for our own targets (CLAUDE.md, plan/features/0001-m0-skeleton.md).
# Interface target hwio::warnings - linked by every hwio target, never by third-party targets.
include_guard(GLOBAL)

add_library(hwio_warnings INTERFACE)
add_library(hwio::warnings ALIAS hwio_warnings)

set(_hwio_clang_warnings
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
    set(_hwio_clang_cl_warnings /W4 /WX /permissive-)
    foreach(_flag IN LISTS _hwio_clang_warnings)
        list(APPEND _hwio_clang_cl_warnings "/clang:${_flag}")
    endforeach()
    target_compile_options(hwio_warnings INTERFACE ${_hwio_clang_cl_warnings})
else()
    target_compile_options(hwio_warnings INTERFACE ${_hwio_clang_warnings})
endif()

unset(_hwio_clang_warnings)
unset(_hwio_clang_cl_warnings)
