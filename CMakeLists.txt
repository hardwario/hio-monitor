# HARDWARIO Monitor v2 - top-level build (plan/features/0001-m0-skeleton.md).
cmake_minimum_required(VERSION 3.28)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# The version comes from git tags (HioVersion.cmake) and must be known before project().
include(HioVersion)
hio_compute_version()

project(hio-monitor
    VERSION "${HIO_VERSION_NUMERIC}"
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

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# No C++20 modules are used; skip the dependency scan (faster builds, simpler tidy runs).
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(HIO_BUILD_TESTS "Build the unit and integration tests" ON)

include(HioCompilerFlags)
include(HioHardening)
include(HioSanitizers)
include(HioCxxFeatureChecks)
include(HioBackendRules)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core LinguistTools)
qt_standard_project_setup(REQUIRES 6.8
    I18N_SOURCE_LANGUAGE en
    I18N_TRANSLATED_LANGUAGES cs)

include(HioDeps)

hio_generate_version_header()

if(HIO_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(libs/core)
add_subdirectory(app/cli)
add_subdirectory(app/gui)

include(HioTranslations)

if(HIO_BUILD_TESTS)
    add_subdirectory(tests/cmake)
endif()

# The backend link rule (ADR-0004) is checked automatically at the end of this file
# (HioBackendRules.cmake schedules it with cmake_language(DEFER)).
