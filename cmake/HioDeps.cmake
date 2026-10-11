# Third-party dependencies (ADR-0010, plan/features/0001-m0-skeleton.md).
#
# Every dependency is pinned to a release archive URL and its SHA-256 (or, under ADR-0064, to the
# archive of one exact commit). Adding or upgrading a dependency needs user approval in a feature
# plan. Versions and hashes are recorded in plan/features/0001-m0-skeleton.md (CPM.cmake,
# GoogleTest, CLI11) and plan/features/0002-m1-jlink-rtt.md (replxx).
#
# CPM_SOURCE_CACHE (cache variable or environment) enables a shared, offline-capable source
# cache; the presets point it at the git-ignored <source>/.cpm-cache/.
include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/CPM.cmake")

# Third-party code is never linted and never gets our warning flags (those live on hio::warnings).
set(_hio_saved_clang_tidy "${CMAKE_CXX_CLANG_TIDY}")
set(CMAKE_CXX_CLANG_TIDY "")

# CLI11 2.7.2 - BSD-3-Clause - command-line parsing for `hio` (ADR-0017). Header-only mode.
CPMAddPackage(
    NAME CLI11
    VERSION 2.7.2
    URL "https://github.com/CLIUtils/CLI11/releases/download/v2.7.2/CLI11-2.7.2-Source.tar.gz"
    URL_HASH "SHA256=0cb0ef44c4c7129ea972505ee81e6d09c71b2e7fe59ca3a13c47d8a994d682b2"
    SYSTEM YES
    EXCLUDE_FROM_ALL YES
    OPTIONS
        "CLI11_PRECOMPILED OFF"
        "CLI11_SINGLE_FILE OFF"
        "CLI11_BUILD_TESTS OFF"
        "CLI11_BUILD_EXAMPLES OFF"
        "CLI11_BUILD_DOCS OFF"
        "CLI11_INSTALL OFF")

# replxx - BSD-3-Clause - line editing, history and Ctrl+R search for the interactive
# `hio chester app console` (ADR-0033). Only app/cli links it.
# Pinned to a commit (ADR-0064): master 1f149bfe20bf6e49c1afd4154eaf0032c8c2fda2 of 2021-11-25,
# the newest commit. The only tag, release-0.0.4 (2021-10-21), lacks the fixes that came after it
# and that the console needs: printing from another thread while input() runs on Windows
# consoles (dd2cd8d, ba84204: "Keep lock during whole print() on Windows"), enabling raw mode under
# the lock (f1bf1a5) and position tracking without colors (76ee383).
CPMAddPackage(
    NAME replxx
    URL "https://github.com/AmokHuginnsson/replxx/archive/1f149bfe20bf6e49c1afd4154eaf0032c8c2fda2.tar.gz"
    URL_HASH "SHA256=ee6d6c92efd060717c884f10293f32692bec17a2956d10cfcedfee5618c9a5ab"
    SYSTEM YES
    EXCLUDE_FROM_ALL YES
    OPTIONS
        "REPLXX_BUILD_EXAMPLES OFF"
        "REPLXX_BUILD_PACKAGE OFF")

if(HIO_BUILD_TESTS)
    # GoogleTest + GMock 1.18.0 - BSD-3-Clause - unit tests only.
    CPMAddPackage(
        NAME googletest
        VERSION 1.18.0
        URL "https://github.com/google/googletest/releases/download/v1.18.0/googletest-1.18.0.tar.gz"
        URL_HASH "SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5"
        SYSTEM YES
        EXCLUDE_FROM_ALL YES
        OPTIONS
            "INSTALL_GTEST OFF"
            "BUILD_GMOCK ON"
            "gtest_force_shared_crt ON")
    include(GoogleTest)
endif()

set(CMAKE_CXX_CLANG_TIDY "${_hio_saved_clang_tidy}")
unset(_hio_saved_clang_tidy)
