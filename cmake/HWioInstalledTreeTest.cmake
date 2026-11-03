# CTest Packaging.InstalledTreeSelfTest (plan/features/0015 A2): installs the build tree into a
# private prefix (with the Qt deploy step) and checks that the installed tree works on its own:
#   - `hwio --version --json` runs and reports the package kind;
#   - `hwio-monitor --self-test` with QT_QPA_PLATFORM=offscreen loads the main QML and exits 0;
#   - the Czech Qt catalog and the licence files are present.
# Qt search-path variables are cleared so that only the installed tree is used.
#
# Script mode: cmake -DHWIO_BUILD_DIR=... -DHWIO_PREFIX=... -DHWIO_BIN_DIR=... -DHWIO_PACKAGE_KIND=...
#              [-DHWIO_CONFIG=...] -P cmake/HWioInstalledTreeTest.cmake

foreach(_var HWIO_BUILD_DIR HWIO_PREFIX HWIO_BIN_DIR HWIO_PACKAGE_KIND)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "${_var} is not set")
    endif()
endforeach()

file(REMOVE_RECURSE "${HWIO_PREFIX}")
set(_config_args "")
if(HWIO_CONFIG)
    set(_config_args --config "${HWIO_CONFIG}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${HWIO_BUILD_DIR}" --prefix "${HWIO_PREFIX}" ${_config_args}
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
set(_hwio "${HWIO_BIN_DIR}/hwio${_exe}")
set(_gui "${HWIO_BIN_DIR}/hwio-monitor${_exe}")
foreach(_file "${_hwio}" "${_gui}")
    if(NOT EXISTS "${_file}")
        message(FATAL_ERROR "Not installed: ${_file}")
    endif()
endforeach()

# A private home, no Qt search paths from the environment.
set(_home "${HWIO_PREFIX}-home")
file(REMOVE_RECURSE "${_home}")
file(MAKE_DIRECTORY "${_home}/runtime")
file(CHMOD "${_home}/runtime" DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
set(_env
    "HOME=${_home}" "XDG_CONFIG_HOME=${_home}/config" "XDG_DATA_HOME=${_home}/data"
    "XDG_CACHE_HOME=${_home}/cache" "XDG_RUNTIME_DIR=${_home}/runtime"
    "APPDATA=${_home}/appdata" "LOCALAPPDATA=${_home}/localappdata"
    "QT_QPA_PLATFORM=offscreen" "HWIO_NO_CRASH_HANDLER=1")
set(_unset
    --unset=QT_PLUGIN_PATH --unset=QML2_IMPORT_PATH --unset=QML_IMPORT_PATH
    --unset=LD_LIBRARY_PATH --unset=DYLD_LIBRARY_PATH --unset=DYLD_FRAMEWORK_PATH
    --unset=QT_QPA_PLATFORM_PLUGIN_PATH)

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env ${_unset} ${_env} "${_hwio}" --version --json
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error
    TIMEOUT 60)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Installed hwio --version --json failed (${_result}):\n${_output}\n${_error}")
endif()
string(JSON _package ERROR_VARIABLE _json_error GET "${_output}" data package)
if(_json_error OR NOT _package STREQUAL HWIO_PACKAGE_KIND)
    message(FATAL_ERROR
        "hwio --version --json: expected \"package\": \"${HWIO_PACKAGE_KIND}\", got:\n${_output}")
endif()
message(STATUS "hwio --version --json: ${_output}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env ${_unset} ${_env} "${_gui}" --self-test
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error
    TIMEOUT 120)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR
        "Installed hwio-monitor --self-test failed (${_result}):\n${_output}\n${_error}")
endif()
message(STATUS "hwio-monitor --self-test: OK\n${_output}")

# Czech Qt catalog and licences.
file(GLOB_RECURSE _qtbase_cs "${HWIO_PREFIX}/*qtbase_cs.qm")
if(NOT _qtbase_cs)
    message(FATAL_ERROR "qtbase_cs.qm is not in the installed tree (${HWIO_PREFIX}).")
endif()
foreach(_licence LICENSE THIRD-PARTY-NOTICES.txt Qt-LGPL-3.0.txt Qt-SOURCE.txt)
    # The file at any depth (Contents/Resources/licenses/ in the macOS bundle), in a licenses/.
    file(GLOB_RECURSE _found "${HWIO_PREFIX}/${_licence}")
    list(FILTER _found INCLUDE REGEX "/licenses/[^/]+$")
    if(NOT _found)
        message(FATAL_ERROR "licenses/${_licence} is not in the installed tree (${HWIO_PREFIX}).")
    endif()
endforeach()
