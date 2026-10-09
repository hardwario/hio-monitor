# app/cli - the `hio` command-line interface (ADR-0017, ADR-0018). English only (ADR-0027).
#
# hio_cli_impl holds everything except main() so that the unit tests can link it.
# QtCore only; no translations are embedded.

add_library(hio_cli_impl STATIC
    src/cli_app.cpp
    src/cli_app.h
    src/cli_log.cpp
    src/cli_log.h
    src/exit_codes.cpp
    src/exit_codes.h
    src/output.cpp
    src/output.h
    src/selftest.cpp
    src/selftest.h
    src/signals.cpp
    src/signals.h)
hio_apply_target_defaults(hio_cli_impl)
target_include_directories(hio_cli_impl PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hio_cli_impl PUBLIC hio::core Qt6::Core CLI11::CLI11)
if(WIN32)
    target_compile_definitions(hio_cli_impl PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

qt_add_executable(hio src/main.cpp)
hio_apply_target_defaults(hio)
target_link_libraries(hio PRIVATE hio_cli_impl)
# A console application on every OS (no WIN32_EXECUTABLE, no MACOSX_BUNDLE).
set_target_properties(hio PROPERTIES WIN32_EXECUTABLE OFF MACOSX_BUNDLE OFF)

if(HIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()
