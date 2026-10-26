# CTest Packaging.InstalledTreeSelfTest (plan/features/0015 A2): installs the build tree into a
# private prefix (with the Qt deploy step) and checks that the installed tree works on its own:
#   - `hio --version --json` runs and reports the package kind;
#   - `hio-monitor --self-test` with QT_QPA_PLATFORM=offscreen loads the main QML and exits 0;
#   - the Czech Qt catalog and the licence files are present.
# Qt search-path variables are cleared so that only the installed tree is used.
#
# Script mode: cmake -DHIO_BUILD_DIR=... -DHIO_PREFIX=... -DHIO_BIN_DIR=... -DHIO_PACKAGE_KIND=...
#              [-DHIO_CONFIG=...] -P cmake/HioInstalledTreeTest.cmake

foreach(_var HIO_BUILD_DIR HIO_PREFIX HIO_BIN_DIR HIO_PACKAGE_KIND)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "${_var} is not set")
    endif()
endforeach()

file(REMOVE_RECURSE "${HIO_PREFIX}")
set(_config_args "")
if(HIO_CONFIG)
    set(_config_args --config "${HIO_CONFIG}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${HIO_BUILD_DIR}" --prefix "${HIO_PREFIX}" ${_config_args}
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _output)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "cmake --install failed (${_result}):\n${_output}")
endif()

if(CMAKE_HOST_WIN32)
    set(_exe ".exe")
else()
    set(_exe "")
endif()
set(_hio "${HIO_BIN_DIR}/hio${_exe}")
set(_gui "${HIO_BIN_DIR}/hio-monitor${_exe}")
foreach(_file "${_hio}" "${_gui}")
    if(NOT EXISTS "${_file}")
        message(FATAL_ERROR "Not installed: ${_file}")
    endif()
endforeach()

# A private home, no Qt search paths from the environment.
set(_home "${HIO_PREFIX}-home")
file(REMOVE_RECURSE "${_home}")
file(MAKE_DIRECTORY "${_home}/runtime")
file(CHMOD "${_home}/runtime" DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
set(_env
    "HOME=${_home}" "XDG_CONFIG_HOME=${_home}/config" "XDG_DATA_HOME=${_home}/data"
    "XDG_CACHE_HOME=${_home}/cache" "XDG_RUNTIME_DIR=${_home}/runtime"
    "APPDATA=${_home}/appdata" "LOCALAPPDATA=${_home}/localappdata"
    "QT_QPA_PLATFORM=offscreen" "HIO_NO_CRASH_HANDLER=1")
set(_unset
    --unset=QT_PLUGIN_PATH --unset=QML2_IMPORT_PATH --unset=QML_IMPORT_PATH
    --unset=LD_LIBRARY_PATH --unset=DYLD_LIBRARY_PATH --unset=DYLD_FRAMEWORK_PATH
    --unset=QT_QPA_PLATFORM_PLUGIN_PATH)

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env ${_unset} ${_env} "${_hio}" --version --json
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error
    TIMEOUT 60)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Installed hio --version --json failed (${_result}):\n${_output}\n${_error}")
endif()
string(JSON _package ERROR_VARIABLE _json_error GET "${_output}" data package)
if(_json_error OR NOT _package STREQUAL HIO_PACKAGE_KIND)
    message(FATAL_ERROR
        "hio --version --json: expected \"package\": \"${HIO_PACKAGE_KIND}\", got:\n${_output}")
endif()
message(STATUS "hio --version --json: ${_output}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env ${_unset} ${_env} "${_gui}" --self-test
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error
    TIMEOUT 120)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR
        "Installed hio-monitor --self-test failed (${_result}):\n${_output}\n${_error}")
endif()
message(STATUS "hio-monitor --self-test: OK\n${_output}")

# Czech Qt catalog and licences.
file(GLOB_RECURSE _qtbase_cs "${HIO_PREFIX}/*qtbase_cs.qm")
if(NOT _qtbase_cs)
    message(FATAL_ERROR "qtbase_cs.qm is not in the installed tree (${HIO_PREFIX}).")
endif()
foreach(_licence LICENSE THIRD-PARTY-NOTICES.txt Qt-LGPL-3.0.txt Qt-SOURCE.txt)
    file(GLOB_RECURSE _found "${HIO_PREFIX}/*licenses/${_licence}")
    if(NOT _found)
        message(FATAL_ERROR "licenses/${_licence} is not in the installed tree (${HIO_PREFIX}).")
    endif()
endforeach()
