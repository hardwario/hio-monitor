# ADR-0187 (plan/features/0025, M-5): the built executables carry the compiler and linker
# hardening of cmake/HWioHardening.cmake.
#
#   cmake -DHWIO_OS=Linux|Windows -DHWIO_READER=<readelf | llvm-readobj> "-DHWIO_BINARIES=a;b"
#         [-DHWIO_EXPECT_FORTIFY=ON] ["-DHWIO_ARCHIVES=libx.a;..."] -P check_binary_hardening.cmake
#
# HWIO_ARCHIVES (Linux, review R6): static libraries of vendored third-party code (miniz) whose
#   objects must be built with the stack protector (__stack_chk_fail referenced).
#
# Linux (readelf): BIND_NOW (full RELRO), a GNU_RELRO segment, PIE (ELF type DYN), a
#   non-executable GNU_STACK, the stack protector (__stack_chk_fail) and, with
#   HWIO_EXPECT_FORTIFY (Release / RelWithDebInfo), at least one fortified libc call (*_chk).
# Windows (llvm-readobj): Control Flow Guard (GUARD_CF and the load config's GuardCFFunctionTable),
#   DYNAMIC_BASE, HIGH_ENTROPY_VA, NX_COMPAT and CET compatibility (extended DLL characteristics).

cmake_minimum_required(VERSION 3.25)

if(NOT HWIO_READER OR NOT HWIO_BINARIES)
    message(FATAL_ERROR "binary_hardening_check: HWIO_READER and HWIO_BINARIES are required")
endif()

set(_problems 0)
function(_hwio_require binary text regex what)
    if(NOT text MATCHES "${regex}")
        message(SEND_ERROR "${binary}: ${what} is missing (ADR-0187)")
        math(EXPR _count "${_problems} + 1")
        set(_problems ${_count} PARENT_SCOPE)
    endif()
endfunction()

function(_hwio_run out)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE _result OUTPUT_VARIABLE _output
                    ERROR_VARIABLE _error)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "binary_hardening_check: '${ARGN}' failed: ${_error}")
    endif()
    set(${out} "${_output}" PARENT_SCOPE)
endfunction()

foreach(_binary IN LISTS HWIO_BINARIES)
    get_filename_component(_name "${_binary}" NAME)
    if(HWIO_OS STREQUAL "Linux")
        _hwio_run(_header "${HWIO_READER}" --wide --file-header "${_binary}")
        _hwio_run(_dynamic "${HWIO_READER}" --wide --dynamic "${_binary}")
        _hwio_run(_segments "${HWIO_READER}" --wide --program-headers "${_binary}")
        _hwio_run(_symbols "${HWIO_READER}" --wide --dyn-syms "${_binary}")
        _hwio_require("${_name}" "${_header}" "Type:[ \t]+DYN" "PIE (ELF type DYN)")
        _hwio_require("${_name}" "${_dynamic}" "BIND_NOW|FLAGS[^\n]*NOW" "BIND_NOW (-z now)")
        _hwio_require("${_name}" "${_segments}" "GNU_RELRO" "GNU_RELRO (-z relro)")
        # GNU_STACK with flags RW, not RWE (-z noexecstack).
        if(NOT _segments MATCHES "GNU_STACK[^\n]* RW  " OR _segments MATCHES "GNU_STACK[^\n]* RWE")
            message(SEND_ERROR "${_name}: a non-executable GNU_STACK is missing (ADR-0187)")
            math(EXPR _problems "${_problems} + 1")
        endif()
        _hwio_require("${_name}" "${_symbols}" "__stack_chk_fail"
                      "the stack protector (__stack_chk_fail)")
        if(HWIO_EXPECT_FORTIFY)
            _hwio_require("${_name}" "${_symbols}" "__[a-z_0-9]+_chk[@ \n]"
                          "a fortified libc call (*_chk, _FORTIFY_SOURCE)")
        endif()
    elseif(HWIO_OS STREQUAL "Windows")
        _hwio_run(_headers "${HWIO_READER}" --file-headers --coff-load-config
                  --coff-debug-directory "${_binary}")
        _hwio_require("${_name}" "${_headers}" "IMAGE_DLL_CHARACTERISTICS_GUARD_CF"
                      "Control Flow Guard (/guard:cf)")
        _hwio_require("${_name}" "${_headers}" "GuardCFFunctionTable" "the CFG function table")
        _hwio_require("${_name}" "${_headers}" "IMAGE_DLL_CHARACTERISTICS_DYNAMIC_BASE"
                      "DYNAMIC_BASE (/DYNAMICBASE)")
        _hwio_require("${_name}" "${_headers}" "IMAGE_DLL_CHARACTERISTICS_HIGH_ENTROPY_VA"
                      "HIGH_ENTROPY_VA (/HIGHENTROPYVA)")
        _hwio_require("${_name}" "${_headers}" "IMAGE_DLL_CHARACTERISTICS_NX_COMPAT"
                      "NX_COMPAT (/NXCOMPAT)")
        _hwio_require("${_name}" "${_headers}" "CET_COMPAT" "CET compatibility (/CETCOMPAT)")
    else()
        message(FATAL_ERROR "binary_hardening_check: unsupported HWIO_OS '${HWIO_OS}'")
    endif()
endforeach()

if(HWIO_OS STREQUAL "Linux")
    foreach(_archive IN LISTS HWIO_ARCHIVES)
        get_filename_component(_name "${_archive}" NAME)
        _hwio_run(_archive_symbols "${HWIO_READER}" --wide --syms "${_archive}")
        _hwio_require("${_name}" "${_archive_symbols}" "__stack_chk_fail"
                      "the stack protector in the vendored objects (__stack_chk_fail)")
    endforeach()
endif()

if(_problems GREATER 0)
    message(FATAL_ERROR "binary_hardening_check: ${_problems} problem(s)")
endif()
list(LENGTH HWIO_BINARIES _count)
message(STATUS "binary_hardening_check: ${_count} executables hardened")
