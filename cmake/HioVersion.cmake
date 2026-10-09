# Version from git tags (plan/0005-build-ci-release.md, plan/features/0001-m0-skeleton.md).
#
# Tags have the form v2.X.Y or v2.X.Y-<prerelease> (e.g. v2.0.0-rc.1). Resulting versions:
#   exactly on v2.0.0                 -> 2.0.0
#   exactly on v2.0.0-rc.1            -> 2.0.0-rc.1
#   5 commits after v2.0.0            -> 2.0.1-dev.5+abc1234
#   5 commits after v2.0.0-rc.1       -> 2.0.0-rc.1.dev.5+abc1234
#   no v2.* tag reachable             -> 2.0.0-dev+abc1234
#   uncommitted changes               -> build metadata gets ".dirty" (e.g. 2.0.0-dev+abc1234.dirty)
#   no git (e.g. source tarball)      -> HIO_VERSION_OVERRIDE must be set (e.g. -DHIO_VERSION_OVERRIDE=2.0.0)
#
# hio_compute_version() runs before project() and sets HIO_VERSION, HIO_VERSION_NUMERIC (X.Y.Z for
# project()) and HIO_GIT_HASH. hio_generate_version_header() writes hio/core/version_generated.h
# into the build tree. CMake re-configures when HEAD or the checked-out branch ref changes.
include_guard(GLOBAL)

set(HIO_VERSION_OVERRIDE "" CACHE STRING
    "Explicit version (X.Y.Z[-pre][+build]) used when the source tree is not a git checkout")

function(_hio_git out_var)
    execute_process(
        COMMAND "${HIO_GIT_EXECUTABLE}" ${ARGN}
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
function(_hio_track_git_head)
    _hio_git(_head_path rev-parse --git-path HEAD)
    if(NOT _head_path)
        return()
    endif()
    cmake_path(ABSOLUTE_PATH _head_path BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    set(_depends "${_head_path}")
    _hio_git(_ref symbolic-ref -q HEAD)
    if(_ref)
        _hio_git(_ref_path rev-parse --git-path "${_ref}")
        cmake_path(ABSOLUTE_PATH _ref_path BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        if(EXISTS "${_ref_path}")
            list(APPEND _depends "${_ref_path}")
        endif()
    endif()
    _hio_git(_packed_path rev-parse --git-path packed-refs)
    if(_packed_path)
        cmake_path(ABSOLUTE_PATH _packed_path BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        if(EXISTS "${_packed_path}")
            list(APPEND _depends "${_packed_path}")
        endif()
    endif()
    set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" APPEND PROPERTY
        CMAKE_CONFIGURE_DEPENDS ${_depends})
endfunction()

macro(hio_compute_version)
    if(HIO_VERSION_OVERRIDE)
        if(NOT HIO_VERSION_OVERRIDE MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)([-+].*)?$")
            message(FATAL_ERROR "HIO_VERSION_OVERRIDE '${HIO_VERSION_OVERRIDE}' is not X.Y.Z[-pre][+build].")
        endif()
        set(HIO_VERSION "${HIO_VERSION_OVERRIDE}")
        set(HIO_VERSION_NUMERIC "${CMAKE_MATCH_1}")
        set(HIO_GIT_HASH "unknown")
    else()
        find_program(HIO_GIT_EXECUTABLE NAMES git)
        set(_hio_describe "")
        if(HIO_GIT_EXECUTABLE)
            _hio_git(_hio_describe describe --tags --match "v2.*" --long --dirty --always --abbrev=7)
        endif()
        if(NOT _hio_describe)
            message(FATAL_ERROR
                "Cannot determine the version: this is not a git checkout (or git is missing). "
                "Configure with -DHIO_VERSION_OVERRIDE=X.Y.Z.")
        endif()
        _hio_track_git_head()

        set(_hio_build_meta "")
        if(_hio_describe MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)(-([0-9A-Za-z.]+))?-([0-9]+)-g([0-9a-f]+)(-dirty)?$")
            set(_hio_major "${CMAKE_MATCH_1}")
            set(_hio_minor "${CMAKE_MATCH_2}")
            set(_hio_patch "${CMAKE_MATCH_3}")
            set(_hio_pre "${CMAKE_MATCH_5}")
            set(_hio_distance "${CMAKE_MATCH_6}")
            set(HIO_GIT_HASH "${CMAKE_MATCH_7}")
            set(_hio_dirty "${CMAKE_MATCH_8}")
            if(_hio_distance EQUAL 0)
                if(_hio_pre)
                    set(HIO_VERSION "${_hio_major}.${_hio_minor}.${_hio_patch}-${_hio_pre}")
                else()
                    set(HIO_VERSION "${_hio_major}.${_hio_minor}.${_hio_patch}")
                endif()
            elseif(_hio_pre)
                set(HIO_VERSION "${_hio_major}.${_hio_minor}.${_hio_patch}-${_hio_pre}.dev.${_hio_distance}")
                set(_hio_build_meta "${HIO_GIT_HASH}")
            else()
                math(EXPR _hio_patch "${_hio_patch} + 1")
                set(HIO_VERSION "${_hio_major}.${_hio_minor}.${_hio_patch}-dev.${_hio_distance}")
                set(_hio_build_meta "${HIO_GIT_HASH}")
            endif()
            set(HIO_VERSION_NUMERIC "${_hio_major}.${_hio_minor}.${_hio_patch}")
        elseif(_hio_describe MATCHES "^([0-9a-f]+)(-dirty)?$")
            set(HIO_GIT_HASH "${CMAKE_MATCH_1}")
            set(_hio_dirty "${CMAKE_MATCH_2}")
            set(HIO_VERSION "2.0.0-dev")
            set(HIO_VERSION_NUMERIC "2.0.0")
            set(_hio_build_meta "${HIO_GIT_HASH}")
        else()
            message(FATAL_ERROR "Unexpected 'git describe' output '${_hio_describe}'.")
        endif()

        if(_hio_dirty)
            if(_hio_build_meta)
                string(APPEND _hio_build_meta ".dirty")
            else()
                set(_hio_build_meta "dirty")
            endif()
        endif()
        if(_hio_build_meta)
            string(APPEND HIO_VERSION "+${_hio_build_meta}")
        endif()
    endif()
    message(STATUS "HARDWARIO Monitor version: ${HIO_VERSION} (git ${HIO_GIT_HASH})")
endmacro()

# Generates <build>/generated/hio/core/version_generated.h; target hio_version_info exposes it.
function(hio_generate_version_header)
    set(HIO_COMPILER_DESCRIPTION "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        string(APPEND HIO_COMPILER_DESCRIPTION " (clang-cl)")
    endif()
    configure_file(
        "${PROJECT_SOURCE_DIR}/cmake/version.h.in"
        "${PROJECT_BINARY_DIR}/generated/hio/core/version_generated.h"
        @ONLY)
    add_library(hio_version_info INTERFACE)
    add_library(hio::version_info ALIAS hio_version_info)
    target_include_directories(hio_version_info INTERFACE "${PROJECT_BINARY_DIR}/generated")
endfunction()
