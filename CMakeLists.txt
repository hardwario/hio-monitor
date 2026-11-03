# HARDWARIO Monitor v2 - top-level build (plan/features/0001-m0-skeleton.md).
cmake_minimum_required(VERSION 3.28)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# The version comes from git tags (HWioVersion.cmake) and must be known before project().
include(HWioVersion)
hwio_compute_version()

project(hwio-monitor
    VERSION "${HWIO_VERSION_NUMERIC}"
    DESCRIPTION "HARDWARIO Monitor - console, log and flashing tool for HARDWARIO devices"
    HOMEPAGE_URL "https://www.hardwario.com"
    LANGUAGES CXX)

# ADR-0001: clang only (clang / AppleClang on Linux and macOS, clang-cl on Windows).
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(Clang|AppleClang)$")
    message(FATAL_ERROR
        "HARDWARIO Monitor must be built with clang (ADR-0001): clang on Linux, Apple clang on "
        "macOS, clang-cl on Windows. Detected compiler: ${CMAKE_CXX_COMPILER_ID} "
        "(${CMAKE_CXX_COMPILER}). Select a preset from CMakePresets.json.")
endif()
if(WIN32 AND NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    message(FATAL_ERROR
        "On Windows HARDWARIO Monitor must be built with clang-cl (MSVC ABI, ADR-0001), "
        "not with the GNU-style clang driver.")
endif()

# ADR-0207: LLVM lld on Linux, for every target (own and third-party).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(CheckLinkerFlag)
    check_linker_flag(CXX "-fuse-ld=lld" HWIO_HAVE_LLD)
    if(NOT HWIO_HAVE_LLD)
        message(FATAL_ERROR
            "HARDWARIO Monitor links with LLVM lld on Linux (ADR-0207), but -fuse-ld=lld does not "
            "work with ${CMAKE_CXX_COMPILER}. Install lld of the same LLVM version (e.g. lld-21).")
    endif()
    add_link_options("-fuse-ld=lld")
endif()

# ADR-0180: Apple's libc++ (Xcode 16) still ships std::jthread / std::stop_token as experimental.
# The flag enables them and links Apple's static libc++experimental.a (nothing extra to ship). Global,
# so that the feature checks and every target, own or third-party, see the same library.
if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    string(APPEND CMAKE_CXX_FLAGS " -fexperimental-library")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# No C++20 modules are used; skip the dependency scan (faster builds, simpler tidy runs).
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(HWIO_BUILD_TESTS "Build the unit and integration tests" ON)
# Plan 0028 (review round 1, R5): the translation lint needs Python 3; CI requires it so the test
# is never skipped there (a developer machine without Python only gets a warning).
option(HWIO_REQUIRE_PYTHON "Fail the configure step when Python 3 (translation lint) is missing" OFF)
# libFuzzer targets (ADR-0152): only the linux-fuzz preset turns this on; it needs
# HWIO_SANITIZERS to contain "fuzzer" (cmake/HWioSanitizers.cmake).
option(HWIO_BUILD_FUZZERS "Build the libFuzzer targets (linux-fuzz preset only)" OFF)
# Test-only overrides compiled into the product (HWIO_CATALOG_URL; plan/features/0003 reviews R8,
# R12, R18). OFF by default; only the Debug presets (tests, sanitizers, coverage, tidy - all OSes)
# turn it ON. Shipped/packaged binaries are built with it OFF (plan/0005-build-ci-release.md):
#  - a single-config release-type build (any spelling: the check is case-insensitive) with it ON -
#    also from a stale cache - fails;
#  - with a multi-config generator the define is generated per configuration and never reaches
#    Release, RelWithDebInfo or MinSizeRel (genex $<CONFIG:...>, case-insensitive);
#  - HWIO_ALLOW_TEST_OVERRIDES_IN_RELEASE=ON allows it for ONE configure run: it is read and removed
#    from the cache again (not sticky), with a warning.
# Targets add ${HWIO_TEST_OVERRIDES_DEFINE} to their compile definitions.
option(HWIO_TEST_OVERRIDES "Compile in test-only environment overrides (never for shipped builds)"
       OFF)
set(_hwio_allow_overrides_in_release OFF)
if(HWIO_ALLOW_TEST_OVERRIDES_IN_RELEASE)
    set(_hwio_allow_overrides_in_release ON)
    message(WARNING
        "HWIO_ALLOW_TEST_OVERRIDES_IN_RELEASE is ON for this configure run only: test-only overrides "
        "may be compiled into a release-type build. Never ship such a build.")
endif()
unset(HWIO_ALLOW_TEST_OVERRIDES_IN_RELEASE CACHE)
string(TOUPPER "${CMAKE_BUILD_TYPE}" _hwio_build_type)
if(HWIO_TEST_OVERRIDES AND NOT _hwio_allow_overrides_in_release
   AND _hwio_build_type MATCHES "^(RELEASE|RELWITHDEBINFO|MINSIZEREL)$")
    message(FATAL_ERROR
        "HWIO_TEST_OVERRIDES=ON in a ${CMAKE_BUILD_TYPE} build: shipped binaries must not honour "
        "test-only overrides such as HWIO_CATALOG_URL. Configure with -DHWIO_TEST_OVERRIDES=OFF "
        "(a stale cache may still hold ON), or pass -DHWIO_ALLOW_TEST_OVERRIDES_IN_RELEASE=ON for "
        "one local experiment.")
endif()
set(HWIO_TEST_OVERRIDES_DEFINE "")
if(HWIO_TEST_OVERRIDES AND _hwio_allow_overrides_in_release)
    set(HWIO_TEST_OVERRIDES_DEFINE HWIO_TEST_OVERRIDES)
elseif(HWIO_TEST_OVERRIDES)
    set(HWIO_TEST_OVERRIDES_DEFINE
        "$<$<NOT:$<CONFIG:Release,RelWithDebInfo,MinSizeRel>>:HWIO_TEST_OVERRIDES>")
endif()
unset(_hwio_build_type)
unset(_hwio_allow_overrides_in_release)

include(HWioCompilerFlags)
include(HWioHardening)
include(HWioSanitizers)
include(HWioCxxFeatureChecks)
include(HWioBackendRules)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Network Bluetooth LinguistTools)
qt_standard_project_setup(REQUIRES 6.8
    I18N_SOURCE_LANGUAGE en
    I18N_TRANSLATED_LANGUAGES cs)

include(HWioDeps)
include(HWioPackaging)

hwio_generate_version_header()
hwio_generate_package_kind_header()

if(HWIO_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(libs/core)
add_subdirectory(libs/firmware)
add_subdirectory(libs/update)
add_subdirectory(libs/hw/jlink)
add_subdirectory(libs/hw/nrfutil)
add_subdirectory(libs/hw/ble)
add_subdirectory(libs/device)
add_subdirectory(libs/diag)
add_subdirectory(libs/mcp)
add_subdirectory(libs/mcp_tools)
add_subdirectory(app/cli)
add_subdirectory(app/gui)

include(HWioTranslations)

# Install tree, Qt deployment, CPack and the installed-tree tests (plan/features/0015).
hwio_add_packaging()

if(HWIO_BUILD_TESTS)
    add_subdirectory(tests/cmake)
    add_subdirectory(tests/fake_jlink)
    add_subdirectory(tests/fake_nrfutil)
    add_subdirectory(tests/integration)
endif()

# The backend link rule (ADR-0004) is checked automatically at the end of this file
# (HWioBackendRules.cmake schedules it with cmake_language(DEFER)).
