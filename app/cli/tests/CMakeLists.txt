# Unit and end-to-end tests of the `hio` CLI.

add_executable(hio_cli_tests
    cli_app_test.cpp
    e2e_test.cpp
    exit_codes_test.cpp
    main.cpp
    output_test.cpp)
hio_apply_target_defaults(hio_cli_tests)
target_link_libraries(hio_cli_tests PRIVATE hio_cli_impl GTest::gmock)
# The end-to-end tests run the real executable.
target_compile_definitions(hio_cli_tests PRIVATE "HIO_CLI_EXECUTABLE=\"$<TARGET_FILE:hio>\"")
add_dependencies(hio_cli_tests hio)

gtest_discover_tests(hio_cli_tests
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit")
