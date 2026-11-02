# Plan 0028 (ADR-0202): AppViewModel::copyToClipboard is the only place of the GUI that writes the
# clipboard (it also shows the "Copied to clipboard." toast). Fails when another source file of
# app/gui/src calls QGuiApplication::clipboard(). Tests are not scanned.
#   cmake -DHWIO_GUI_SRC=<app/gui/src> -P check_clipboard_writes.cmake

if(NOT IS_DIRECTORY "${HWIO_GUI_SRC}")
    message(FATAL_ERROR "clipboard_write_check: HWIO_GUI_SRC is not a directory: '${HWIO_GUI_SRC}'")
endif()

set(_allowed "viewmodels/app_view_model.cpp")
file(GLOB_RECURSE _sources RELATIVE "${HWIO_GUI_SRC}" "${HWIO_GUI_SRC}/*.cpp" "${HWIO_GUI_SRC}/*.h")
list(SORT _sources)
set(_violations 0)
set(_checked 0)
foreach(_file IN LISTS _sources)
    math(EXPR _checked "${_checked} + 1")
    if(_file STREQUAL _allowed)
        continue()
    endif()
    file(STRINGS "${HWIO_GUI_SRC}/${_file}" _hits REGEX "clipboard\\(\\)")
    foreach(_hit IN LISTS _hits)
        message(SEND_ERROR "${_file}: writes the clipboard itself (use AppViewModel::copyToClipboard, ADR-0202): ${_hit}")
        math(EXPR _violations "${_violations} + 1")
    endforeach()
endforeach()

if(_violations GREATER 0)
    message(FATAL_ERROR "clipboard_write_check: ${_violations} problem(s)")
endif()
message(STATUS "clipboard_write_check: ${_checked} files OK")
