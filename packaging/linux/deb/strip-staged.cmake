# CPack pre-build script of the .deb (plan/features/0015 C2): strips the ELF files of the staged
# package that are not stripped yet (the ICU libraries that come with the Qt binaries; our
# executables are stripped by CPACK_STRIP_FILES). Runs with CPACK_TEMPORARY_INSTALL_DIRECTORY set.
file(GLOB_RECURSE _hio_files LIST_DIRECTORIES false
    "${CPACK_TEMPORARY_INSTALL_DIRECTORY}/opt/hardwario-monitor/*")
foreach(_hio_file IN LISTS _hio_files)
    if(IS_SYMLINK "${_hio_file}")
        continue()
    endif()
    execute_process(COMMAND file -b "${_hio_file}" OUTPUT_VARIABLE _hio_type)
    if(_hio_type MATCHES "ELF" AND _hio_type MATCHES "not stripped")
        message(STATUS "Stripping ${_hio_file}")
        execute_process(COMMAND strip --strip-unneeded --remove-section=.comment
                                --remove-section=.note "${_hio_file}"
                        RESULT_VARIABLE _hio_result)
        if(NOT _hio_result EQUAL 0)
            message(FATAL_ERROR "strip failed: ${_hio_file}")
        endif()
    endif()
endforeach()
