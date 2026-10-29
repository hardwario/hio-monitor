# Version from git tags (plan/0005-build-ci-release.md, plan/features/0001-m0-skeleton.md).
#
# Tags have the form v2.X.Y or v2.X.Y-<prerelease> (e.g. v2.0.0-rc.1). Resulting versions:
#   exactly on v2.0.0                 -> 2.0.0
#   exactly on v2.0.0-rc.1            -> 2.0.0-rc.1
#   5 commits after v2.0.0            -> 2.0.1-dev.5+abc1234
#   5 commits after v2.0.0-rc.1       -> 2.0.0-rc.1.dev.5+abc1234
#   no v2.* tag reachable             -> 2.0.0-dev+abc1234
#   uncommitted changes               -> build metadata gets ".dirty" (e.g. 2.0.0-dev+abc1234.dirty)
#   no git (e.g. source tarball)      -> HWIO_VERSION_OVERRIDE must be set (e.g. -DHWIO_VERSION_OVERRIDE=2.0.0)
#
# hwio_compute_version() runs before project() and sets HWIO_VERSION, HWIO_VERSION_NUMERIC (X.Y.Z for
# project()) and HWIO_GIT_HASH. hwio_generate_version_header() writes hwio/core/version_generated.h
# into the build tree. CMake re-configures when HEAD or the checked-out branch ref changes.
include_guard(GLOBAL)

set(HWIO_VERSION_OVERRIDE "" CACHE STRING
    "Explicit version (X.Y.Z[-pre][+build]) used when the source tree is not a git checkout")

function(_hwio_git out_var)
    execute_process(
        COMMAND "${HWIO_GIT_EXECUTABLE}" ${ARGN}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _output
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_result EQUAL 0)
        set(${out_var} "${_output}" PARENT_SCOPE)
    else()
        set(${out_var} "" PARENT_SCOPE)
    endif()
endfunction()

# Re-run configure when HEAD moves (new commit, checkout) so the version stays correct.
function(_hwio_track_git_head)
    _hwio_git(_head_path rev-parse --git-path HEAD)
    if(NOT _head_path)
        return()
    endif()
    cmake_path(ABSOLUTE_PATH _head_path BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    set(_depends "${_head_path}")
    _hwio_git(_ref symbolic-ref -q HEAD)
    if(_ref)
        _hwio_git(_ref_path rev-parse --git-path "${_ref}")
        cmake_path(ABSOLUTE_PATH _ref_path BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        if(EXISTS "${_ref_path}")
            list(APPEND _depends "${_ref_path}")
        endif()
    endif()
    _hwio_git(_packed_path rev-parse --git-path packed-refs)
    if(_packed_path)
        cmake_path(ABSOLUTE_PATH _packed_path BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        if(EXISTS "${_packed_path}")
            list(APPEND _depends "${_packed_path}")
        endif()
    endif()
    set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" APPEND PROPERTY
        CMAKE_CONFIGURE_DEPENDS ${_depends})
endfunction()

macro(hwio_compute_version)
    if(HWIO_VERSION_OVERRIDE)
        if(NOT HWIO_VERSION_OVERRIDE MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)([-+].*)?$")
            message(FATAL_ERROR "HWIO_VERSION_OVERRIDE '${HWIO_VERSION_OVERRIDE}' is not X.Y.Z[-pre][+build].")
        endif()
        set(HWIO_VERSION "${HWIO_VERSION_OVERRIDE}")
        set(HWIO_VERSION_NUMERIC "${CMAKE_MATCH_1}")
        set(HWIO_GIT_HASH "unknown")
    else()
        find_program(HWIO_GIT_EXECUTABLE NAMES git)
        set(_hwio_describe "")
        if(HWIO_GIT_EXECUTABLE)
            _hwio_git(_hwio_describe describe --tags --match "v2.*" --long --dirty --always --abbrev=7)
        endif()
        if(NOT _hwio_describe)
            message(FATAL_ERROR
                "Cannot determine the version: this is not a git checkout (or git is missing). "
                "Configure with -DHWIO_VERSION_OVERRIDE=X.Y.Z.")
        endif()
        _hwio_track_git_head()

        set(_hwio_build_meta "")
        if(_hwio_describe MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)(-([0-9A-Za-z.]+))?-([0-9]+)-g([0-9a-f]+)(-dirty)?$")
            set(_hwio_major "${CMAKE_MATCH_1}")
            set(_hwio_minor "${CMAKE_MATCH_2}")
            set(_hwio_patch "${CMAKE_MATCH_3}")
            set(_hwio_pre "${CMAKE_MATCH_5}")
            set(_hwio_distance "${CMAKE_MATCH_6}")
            set(HWIO_GIT_HASH "${CMAKE_MATCH_7}")
            set(_hwio_dirty "${CMAKE_MATCH_8}")
            if(_hwio_distance EQUAL 0)
                if(_hwio_pre)
                    set(HWIO_VERSION "${_hwio_major}.${_hwio_minor}.${_hwio_patch}-${_hwio_pre}")
                else()
                    set(HWIO_VERSION "${_hwio_major}.${_hwio_minor}.${_hwio_patch}")
                endif()
            elseif(_hwio_pre)
                set(HWIO_VERSION "${_hwio_major}.${_hwio_minor}.${_hwio_patch}-${_hwio_pre}.dev.${_hwio_distance}")
                set(_hwio_build_meta "${HWIO_GIT_HASH}")
            else()
                math(EXPR _hwio_patch "${_hwio_patch} + 1")
                set(HWIO_VERSION "${_hwio_major}.${_hwio_minor}.${_hwio_patch}-dev.${_hwio_distance}")
                set(_hwio_build_meta "${HWIO_GIT_HASH}")
            endif()
            set(HWIO_VERSION_NUMERIC "${_hwio_major}.${_hwio_minor}.${_hwio_patch}")
        elseif(_hwio_describe MATCHES "^([0-9a-f]+)(-dirty)?$")
            set(HWIO_GIT_HASH "${CMAKE_MATCH_1}")
            set(_hwio_dirty "${CMAKE_MATCH_2}")
            set(HWIO_VERSION "2.0.0-dev")
            set(HWIO_VERSION_NUMERIC "2.0.0")
            set(_hwio_build_meta "${HWIO_GIT_HASH}")
        else()
            message(FATAL_ERROR "Unexpected 'git describe' output '${_hwio_describe}'.")
        endif()

        if(_hwio_dirty)
            if(_hwio_build_meta)
                string(APPEND _hwio_build_meta ".dirty")
            else()
                set(_hwio_build_meta "dirty")
            endif()
        endif()
        if(_hwio_build_meta)
            string(APPEND HWIO_VERSION "+${_hwio_build_meta}")
        endif()
    endif()
    message(STATUS "HARDWARIO Monitor version: ${HWIO_VERSION} (git ${HWIO_GIT_HASH})")
endmacro()

# Generates <build>/generated/hwio/core/version_generated.h; target hwio_version_info exposes it.
function(hwio_generate_version_header)
    set(HWIO_COMPILER_DESCRIPTION "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        string(APPEND HWIO_COMPILER_DESCRIPTION " (clang-cl)")
    endif()
    configure_file(
        "${PROJECT_SOURCE_DIR}/cmake/version.h.in"
        "${PROJECT_BINARY_DIR}/generated/hwio/core/version_generated.h"
        @ONLY)
    add_library(hwio_version_info INTERFACE)
    add_library(hwio::version_info ALIAS hwio_version_info)
    target_include_directories(hwio_version_info INTERFACE "${PROJECT_BINARY_DIR}/generated")
endfunction()
