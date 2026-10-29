# CPack pre-build script of the .deb (plan/features/0015 C2): strips the ELF files of the staged
# package that are not stripped yet (the ICU libraries that come with the Qt binaries; our
# executables are stripped by CPACK_STRIP_FILES). Runs with CPACK_TEMPORARY_INSTALL_DIRECTORY set.
file(GLOB_RECURSE _hwio_files LIST_DIRECTORIES false
    "${CPACK_TEMPORARY_INSTALL_DIRECTORY}/opt/hardwario-monitor/*")
foreach(_hwio_file IN LISTS _hwio_files)
    if(IS_SYMLINK "${_hwio_file}")
        continue()
    endif()
    execute_process(COMMAND file -b "${_hwio_file}" OUTPUT_VARIABLE _hwio_type)
    if(_hwio_type MATCHES "ELF" AND _hwio_type MATCHES "not stripped")
        message(STATUS "Stripping ${_hwio_file}")
        execute_process(COMMAND strip --strip-unneeded --remove-section=.comment
                                --remove-section=.note "${_hwio_file}"
                        RESULT_VARIABLE _hwio_result)
        if(NOT _hwio_result EQUAL 0)
            message(FATAL_ERROR "strip failed: ${_hwio_file}")
        endif()
    endif()
endforeach()
