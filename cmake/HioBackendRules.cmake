# Backend library rules (ADR-0004, ADR-0022, plan/features/0001-m0-skeleton.md).
#
#   hio_add_backend_library(<name> [<source>...])
#       Creates a static library (ADR-0022) with hio::warnings and hio::hardening and registers it
#       as a backend library.
#   hio_apply_target_defaults(<target>)
#       Adds hio::warnings and hio::hardening to any other own target (executables, tests).
#   hio_skip_linting(<target>...)
#       Disables clang-tidy for targets that contain only generated code (e.g. the object
#       libraries returned by OUTPUT_TARGETS of qt_add_resources / qt_add_qml_module).
#   hio_restrict_own_links(<target> ALLOW <hio::target>...)
#       Restricts the own (hio:: / hio_) targets that <target> may link directly
#       (LINK_LIBRARIES and INTERFACE_LINK_LIBRARIES) to the ALLOW list plus hio::warnings and
#       hio::hardening (ADR-0150: hio_mcp links hio::core only). Checked at the end of the
#       configure step together with the backend rule; tests/cmake/backend_links_gui verifies it.
#
# Including this module schedules a check at the end of the top-level configure step
# (cmake_language(DEFER)): every registered backend library's LINK_LIBRARIES and
# INTERFACE_LINK_LIBRARIES are walked transitively and configuring fails if a GUI Qt module
# appears. tests/cmake/backend_links_gui verifies that the check fires.
include_guard(GLOBAL)

set(HIO_FORBIDDEN_BACKEND_LINKS
    Qt6::Gui Qt6::Quick Qt6::Qml Qt6::Widgets Qt6::QuickControls2
    Qt::Gui Qt::Quick Qt::Qml Qt::Widgets Qt::QuickControls2)

define_property(GLOBAL PROPERTY HIO_BACKEND_TARGETS
    BRIEF_DOCS "Backend libraries checked by hio backend rules")
define_property(GLOBAL PROPERTY HIO_RESTRICTED_LINK_TARGETS
    BRIEF_DOCS "Targets whose own (hio::) links are restricted by hio_restrict_own_links")

# Always allowed: added to every own target by hio_apply_target_defaults().
set(HIO_ALWAYS_ALLOWED_OWN_LINKS hio::warnings hio::hardening hio_warnings hio_hardening)

function(hio_apply_target_defaults target)
    target_link_libraries(${target} PRIVATE hio::warnings hio::hardening)
endfunction()

function(hio_skip_linting)
    if(ARGN)
        set_target_properties(${ARGN} PROPERTIES CXX_CLANG_TIDY "" SKIP_LINTING ON)
    endif()
endfunction()

function(hio_add_backend_library name)
    add_library(${name} STATIC ${ARGN})
    hio_apply_target_defaults(${name})
    set_property(GLOBAL APPEND PROPERTY HIO_BACKEND_TARGETS ${name})
endfunction()

function(hio_restrict_own_links target)
    cmake_parse_arguments(PARSE_ARGV 1 _arg "" "" "ALLOW")
    set_property(TARGET ${target} PROPERTY HIO_ALLOWED_OWN_LINKS ${_arg_ALLOW})
    set_property(GLOBAL APPEND PROPERTY HIO_RESTRICTED_LINK_TARGETS ${target})
endfunction()

# Turns one LINK_LIBRARIES entry into a plain item name (or "" when it should be ignored).
function(_hio_normalize_link_item item out_var)
    # Directory scope markers added by CMake for target_link_libraries() from another directory.
    if(item MATCHES "^::@")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()
    # $<LINK_ONLY:X>, $<BUILD_INTERFACE:X>, $<$<CONFIG:Debug>:X>, ... -> X
    while(item MATCHES "^\\$<.*>$")
        string(REGEX REPLACE "^\\$<(.*)>$" "\\1" item "${item}")
        string(FIND "${item}" ":" _colon REVERSE)
        if(_colon LESS 0)
            set(${out_var} "" PARENT_SCOPE)
            return()
        endif()
        # Keep the text after the last top-level ':' that is not part of '::'.
        string(REGEX MATCH "([^:]|::)*$" item "${item}")
        string(REGEX REPLACE "^:" "" item "${item}")
    endwhile()
    set(${out_var} "${item}" PARENT_SCOPE)
endfunction()

function(_hio_check_backend_target root)
    set(_stack "${root}")
    set(_paths "${root}")
    set(_visited "")
    while(_stack)
        list(POP_FRONT _stack _current)
        list(POP_FRONT _paths _path)
        if(_current IN_LIST _visited)
            continue()
        endif()
        list(APPEND _visited "${_current}")

        get_target_property(_aliased "${_current}" ALIASED_TARGET)
        if(_aliased)
            set(_current "${_aliased}")
        endif()
        set(_items "")
        get_target_property(_type "${_current}" TYPE)
        if(NOT _type STREQUAL "INTERFACE_LIBRARY")
            get_target_property(_link "${_current}" LINK_LIBRARIES)
            if(_link)
                list(APPEND _items ${_link})
            endif()
        endif()
        get_target_property(_ilink "${_current}" INTERFACE_LINK_LIBRARIES)
        if(_ilink)
            list(APPEND _items ${_ilink})
        endif()

        foreach(_raw IN LISTS _items)
            _hio_normalize_link_item("${_raw}" _item)
            if(NOT _item)
                continue()
            endif()
            if(_item IN_LIST HIO_FORBIDDEN_BACKEND_LINKS)
                string(REPLACE ";" " -> " _chain "${_path};${_item}")
                message(FATAL_ERROR
                    "Backend library '${root}' links '${_item}' (${_chain}). Backend libraries "
                    "(libs/*) must not use QtGui, QtQuick, QtQml, QtWidgets or QtQuickControls2 "
                    "(ADR-0004).")
            endif()
            if(TARGET "${_item}")
                list(APPEND _stack "${_item}")
                list(APPEND _paths "${_path} -> ${_item}")
            endif()
        endforeach()
    endwhile()
endfunction()

# Direct own links of `target` (hio::x or hio_x) must be in its ALLOW list (hio_restrict_own_links).
function(_hio_check_restricted_target target)
    get_target_property(_allowed "${target}" HIO_ALLOWED_OWN_LINKS)
    if(NOT _allowed)
        set(_allowed "")
    endif()
    # Accept both spellings of every allowed target (alias hio::x and the real name hio_x).
    set(_accepted ${HIO_ALWAYS_ALLOWED_OWN_LINKS})
    foreach(_item IN LISTS _allowed)
        list(APPEND _accepted "${_item}")
        if(TARGET "${_item}")
            get_target_property(_real "${_item}" ALIASED_TARGET)
            if(_real)
                list(APPEND _accepted "${_real}")
            endif()
        endif()
    endforeach()
    set(_items "")
    get_target_property(_link "${target}" LINK_LIBRARIES)
    if(_link)
        list(APPEND _items ${_link})
    endif()
    get_target_property(_ilink "${target}" INTERFACE_LINK_LIBRARIES)
    if(_ilink)
        list(APPEND _items ${_ilink})
    endif()
    foreach(_raw IN LISTS _items)
        _hio_normalize_link_item("${_raw}" _item)
        if(NOT _item OR NOT _item MATCHES "^hio(::|_)")
            continue()
        endif()
        if(NOT _item IN_LIST _accepted)
            message(FATAL_ERROR
                "Library '${target}' links '${_item}', but only ${_allowed} may be linked from "
                "hio:: (ADR-0150).")
        endif()
    endforeach()
endfunction()

function(_hio_check_backend_rules)
    get_property(_targets GLOBAL PROPERTY HIO_BACKEND_TARGETS)
    foreach(_target IN LISTS _targets)
        _hio_check_backend_target("${_target}")
    endforeach()
    list(LENGTH _targets _count)
    message(STATUS "HARDWARIO Monitor: backend link rule checked for ${_count} libraries")
    get_property(_restricted GLOBAL PROPERTY HIO_RESTRICTED_LINK_TARGETS)
    foreach(_target IN LISTS _restricted)
        _hio_check_restricted_target("${_target}")
    endforeach()
    list(LENGTH _restricted _count)
    message(STATUS "HARDWARIO Monitor: own-link restriction checked for ${_count} libraries")
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _hio_check_backend_rules)
