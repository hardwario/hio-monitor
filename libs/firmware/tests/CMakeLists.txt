# Unit tests of libs/firmware (GoogleTest, ADR-0010): HEX parser, zip limits and zip slip, LTE
# input classification, cache and catalog against a local HTTP fixture (plan/features/0003).
# The test-only builders (hex_builder.h, zip_builder.h, http_fixture.h) are shared with the
# libs/device and CLI tests.

add_executable(hio_firmware_tests
    catalog_test.cpp
    designated_test.cpp
    hex_builder.h
    http_fixture.h
    intel_hex_test.cpp
    main.cpp
    zip_builder.h
    zip_test.cpp)
hio_apply_target_defaults(hio_firmware_tests)
target_include_directories(hio_firmware_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(hio_firmware_tests PRIVATE hio::firmware hio::miniz Qt6::Network GTest::gmock)
# Plan 0021: the saved catalog page (data/catalog_page_<id>.html, fetched 2026-10-23).
target_compile_definitions(hio_firmware_tests PRIVATE
    HIO_FIRMWARE_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")

# Under ThreadSanitizer the tests that use QNetworkAccessManager are left out: its HTTP thread lives
# entirely inside the uninstrumented libQt6Network/libQt6Core, so TSan reports Qt's own posted
# events and implicitly shared buffers as races (and ignore_noninstrumented_modules crashes TSan's
# thread registry). FirmwareCatalog has no threads of ours; the tests run in every other preset.
set(_hio_firmware_test_filter "")
if("thread" IN_LIST HIO_SANITIZERS)
    set(_hio_firmware_test_filter
        TEST_FILTER "-Catalog.DownloadsParsesAndCaches:Catalog.NotFound:Catalog.ServerError:Catalog.Timeout:Catalog.Oversize:Catalog.InvalidHexIsNotCached:Catalog.Cancel:Catalog.RedirectsOnlyToHttps:Catalog.CorruptCacheEntryIsDeletedAndDownloadedAgain:Catalog.UnwritableCacheIsAWarning:Designated.*:Lookup.*")
endif()
gtest_discover_tests(hio_firmware_tests
    DISCOVERY_TIMEOUT 60
    ${_hio_firmware_test_filter}
    PROPERTIES LABELS "unit")
