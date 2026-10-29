# ADR-0184 (plan/features/0025, M-2): every Label / Text / TextArea / TextEdit whose `text` is not
# a string literal or qsTr() of a string literal must set `textFormat` (Text.PlainText), so device
# or file strings that look like HTML are never rendered as rich text.
#
#   cmake -DHWIO_QML_DIR=<dir> [-DHWIO_QML_ALLOWLIST=<file>] -P check_qml_plaintext.cmake
#
# The allow-list holds one path per line, relative to HWIO_QML_DIR ('#' starts a comment); those
# files are not checked (only WizardDialog.qml, the single StyledText consumer, ADR-0184).
#
# This is a line-based scanner for the formatting used in app/gui/qml (qmlformat style: one
# property per line, elements opened with `Type {`). TextField is not checked: TextInput has no
# textFormat and always shows plain text.

cmake_minimum_required(VERSION 3.25)

if(NOT HWIO_QML_DIR OR NOT IS_DIRECTORY "${HWIO_QML_DIR}")
    message(FATAL_ERROR "qml_plaintext_check: HWIO_QML_DIR is not a directory: '${HWIO_QML_DIR}'")
endif()

get_filename_component(HWIO_QML_DIR "${HWIO_QML_DIR}" ABSOLUTE)

set(_allowed "")
if(HWIO_QML_ALLOWLIST)
    file(STRINGS "${HWIO_QML_ALLOWLIST}" _allow_lines)
    foreach(_entry IN LISTS _allow_lines)
        string(REGEX REPLACE "#.*$" "" _entry "${_entry}")
        string(STRIP "${_entry}" _entry)
        if(NOT _entry STREQUAL "")
            list(APPEND _allowed "${_entry}")
        endif()
    endforeach()
endif()

set(_checked_types Label Text TextArea TextEdit)
set(_literal_regex
    "^(\"[^\"]*\"|'[^']*'|qsTr[(][ \t]*\"[^\"]*\"([ \t]*,[ \t]*\"[^\"]*\")?[ \t]*[)])[ \t]*(@SEMI@)?[ \t]*(}.*)?$")

# Replaces characters that CMake list handling would misinterpret (';', '[', ']', '\').
function(_hwio_escape_qml text out)
    string(REPLACE "\\\\" "@BB@" text "${text}")
    string(REPLACE "\\\"" "@DQ@" text "${text}")
    string(REPLACE "\\'" "@SQ@" text "${text}")
    string(REPLACE "\\" "@BS@" text "${text}")
    string(REPLACE ";" "@SEMI@" text "${text}")
    string(REPLACE "[" "@LB@" text "${text}")
    string(REPLACE "]" "@RB@" text "${text}")
    set(${out} "${text}" PARENT_SCOPE)
endfunction()

set(_violations 0)
set(_files_checked 0)
file(GLOB_RECURSE _qml_files RELATIVE "${HWIO_QML_DIR}" "${HWIO_QML_DIR}/*.qml")
list(SORT _qml_files)
foreach(_relative IN LISTS _qml_files)
    if(_relative IN_LIST _allowed)
        continue()
    endif()
    math(EXPR _files_checked "${_files_checked} + 1")
    file(READ "${HWIO_QML_DIR}/${_relative}" _content)
    _hwio_escape_qml("${_content}" _content)
    string(REPLACE "\n" ";" _lines "${_content}")
    list(LENGTH _lines _line_count)

    # Stack of open blocks: element type, or "-" for a non-element block (JS, object literal).
    set(_stack "")
    # Per element (keyed by stack depth): start line, text binding line (non-literal), format set.
    set(_in_comment FALSE)
    set(_index 0)
    while(_index LESS _line_count)
        list(GET _lines ${_index} _line)
        math(EXPR _line_number "${_index} + 1")
        math(EXPR _index "${_index} + 1")

        # Block comments (/* ... */), then strings and line comments, leave the structure.
        set(_code "${_line}")
        if(_in_comment)
            string(FIND "${_code}" "*/" _end)
            if(_end EQUAL -1)
                continue()
            endif()
            math(EXPR _end "${_end} + 2")
            string(SUBSTRING "${_code}" ${_end} -1 _code)
            set(_in_comment FALSE)
        endif()
        string(REGEX REPLACE "\"[^\"]*\"" "\"\"" _code "${_code}")
        string(REGEX REPLACE "'[^']*'" "''" _code "${_code}")
        string(REGEX REPLACE "/[*].*[*]/" "" _code "${_code}")
        string(REGEX REPLACE "//.*$" "" _code "${_code}")
        string(FIND "${_code}" "/*" _start)
        if(NOT _start EQUAL -1)
            string(SUBSTRING "${_code}" 0 ${_start} _code)
            set(_in_comment TRUE)
        endif()

        string(REGEX MATCHALL "[A-Za-z_][A-Za-z0-9_.]*[ \t]*[{:]|[{}]" _tokens "${_code}")
        foreach(_token IN LISTS _tokens)
            list(LENGTH _stack _depth)
            if(_token STREQUAL "}")
                if(_depth GREATER 0)
                    list(POP_BACK _stack _closed)
                    if(NOT _closed STREQUAL "-")
                        if(DEFINED _text_${_depth} AND NOT _format_${_depth})
                            message(SEND_ERROR
                                "${_relative}:${_text_${_depth}}: ${_closed} (line "
                                "${_start_${_depth}}) binds 'text' to a non-literal value "
                                "without 'textFormat: Text.PlainText' (ADR-0184)")
                            math(EXPR _violations "${_violations} + 1")
                        endif()
                    endif()
                    unset(_text_${_depth})
                    unset(_format_${_depth})
                endif()
            elseif(_token MATCHES "{$")
                string(REGEX REPLACE "[ \t]*{$" "" _type "${_token}")
                string(REGEX REPLACE "^.*[.]" "" _short "${_type}")
                if(_type STREQUAL "" OR NOT _short MATCHES "^[A-Z]")
                    list(APPEND _stack "-")
                elseif(_short IN_LIST _checked_types)
                    list(APPEND _stack "${_short}")
                else()
                    list(APPEND _stack "*")
                endif()
                list(LENGTH _stack _depth)
                set(_format_${_depth} FALSE)
                set(_start_${_depth} "${_line_number}")
            elseif(_depth GREATER 0)
                list(GET _stack -1 _top)
                if(NOT _top STREQUAL "-" AND NOT _top STREQUAL "*")
                    string(REGEX REPLACE "[ \t]*:$" "" _property "${_token}")
                    if(_property STREQUAL "textFormat")
                        set(_format_${_depth} TRUE)
                        # Rich text formats only in allow-listed files (ADR-0184).
                        if(NOT _line MATCHES "textFormat[ \t]*:[ \t]*(Text|TextEdit)[.]PlainText")
                            message(SEND_ERROR
                                "${_relative}:${_line_number}: ${_top} sets a textFormat other "
                                "than PlainText; only allow-listed files may (ADR-0184)")
                            math(EXPR _violations "${_violations} + 1")
                        endif()
                    elseif(_property STREQUAL "text")
                        # The value as written (the original line, after 'text:').
                        string(REGEX MATCH "(^|[^A-Za-z0-9_.])text[ \t]*:[ \t]*(.*)$" _match
                            "${_line}")
                        set(_value "${CMAKE_MATCH_2}")
                        string(STRIP "${_value}" _value)
                        string(REGEX REPLACE "[ \t]*//.*$" "" _value "${_value}")
                        set(_literal FALSE)
                        if(_value MATCHES "${_literal_regex}")
                            set(_literal TRUE)
                            # A continuation on the next line (".arg(...)", "+ x") is not literal.
                            if(_index LESS _line_count)
                                list(GET _lines ${_index} _next)
                                if(_next MATCHES "^[ \t]*([.+?:]|&&|[|][|])")
                                    set(_literal FALSE)
                                endif()
                            endif()
                        endif()
                        if(NOT _literal)
                            set(_text_${_depth} "${_line_number}")
                        endif()
                    endif()
                endif()
            endif()
        endforeach()
    endwhile()
    list(LENGTH _stack _depth)
    if(NOT _depth EQUAL 0)
        message(SEND_ERROR "${_relative}: unbalanced braces (${_depth} open at the end); "
                           "the scanner cannot check this file")
        math(EXPR _violations "${_violations} + 1")
    endif()
endforeach()

if(_violations GREATER 0)
    message(FATAL_ERROR "qml_plaintext_check: ${_violations} problem(s) in ${_files_checked} files")
endif()
message(STATUS "qml_plaintext_check: ${_files_checked} QML files OK")
