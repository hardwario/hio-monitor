# Licence notices of the packages (ADR-0119, plan/features/0015 A4).
#
# hio_install_licenses(<destination>) installs into <destination> (the `licenses/` directory of the
# installed tree):
#   LICENSE                     HARDWARIO licence (with the LGPLv3 section 4 exception, ADR-0119)
#   Qt-LGPL-3.0.txt GPL-3.0.txt Qt's licence (LGPL-3.0) and the GPL-3.0 it references
#   Qt-SOURCE.txt               generated: the Qt version, the URL of its exact source archive and the
#                               written offer
#   Inter-LICENSE.txt JetBrainsMono-OFL.txt   bundled fonts, OFL-1.1 (ADR-0048)
#   MaterialSymbols-Apache-2.0.txt            icons, Apache-2.0 (ADR-0060)
#   CLI11-BSD-3-Clause.txt replxx-LICENSE.md miniz-MIT.txt
#   THIRD-PARTY-NOTICES.txt     generated index (name, version, licence, URL, file) from the table
#                               below
#
# This file is also run in script mode (cmake -P) by the CTest
# Packaging.LicenceTableCoversCpmDependencies: every CPMAddPackage(NAME ...) of cmake/HioDeps.cmake
# must have a row in HIO_THIRD_PARTY (test-only dependencies with distributed = NO).

# One row per component: name | version | SPDX licence | URL | licence file(s) in licenses/ |
# distributed (YES = in the packages, NO = test-only, not shipped). `cpm:<name>` marks the rows
# that correspond to a CPM dependency of cmake/HioDeps.cmake.
set(HIO_THIRD_PARTY
    "Qt|@QT_VERSION@|LGPL-3.0-only|https://www.qt.io/|Qt-LGPL-3.0.txt, GPL-3.0.txt, Qt-SOURCE.txt|YES|"
    "CLI11|2.7.2|BSD-3-Clause|https://github.com/CLIUtils/CLI11|CLI11-BSD-3-Clause.txt|YES|cpm:CLI11"
    "replxx|1f149bfe20bf6e49c1afd4154eaf0032c8c2fda2|BSD-3-Clause|https://github.com/AmokHuginnsson/replxx|replxx-LICENSE.md|YES|cpm:replxx"
    "miniz|3.1.2|MIT|https://github.com/richgel999/miniz|miniz-MIT.txt|YES|cpm:miniz"
    "GoogleTest|1.18.0|BSD-3-Clause|https://github.com/google/googletest|-|NO|cpm:googletest"
    "Inter|4.1|OFL-1.1|https://github.com/rsms/inter|Inter-LICENSE.txt|YES|"
    "JetBrains Mono|2.304|OFL-1.1|https://github.com/JetBrains/JetBrainsMono|JetBrainsMono-OFL.txt|YES|"
    "Material Symbols|bd8cb85bd4bad964fe6918f79665bb40c3a8efef|Apache-2.0|https://github.com/google/material-design-icons|MaterialSymbols-Apache-2.0.txt|YES|")

if(CMAKE_SCRIPT_MODE_FILE)
    # ---- script mode: every CPM dependency has a row -------------------------------------------
    file(READ "${HIO_DEPS_FILE}" _deps)
    string(REGEX MATCHALL "CPMAddPackage\\([ \t\r\n]*NAME[ \t]+[A-Za-z0-9_.-]+" _calls "${_deps}")
    set(_missing "")
    set(_count 0)
    foreach(_call IN LISTS _calls)
        string(REGEX REPLACE ".*NAME[ \t]+" "" _name "${_call}")
        math(EXPR _count "${_count} + 1")
        set(_found FALSE)
        foreach(_row IN LISTS HIO_THIRD_PARTY)
            if(_row MATCHES "\\|cpm:${_name}$")
                set(_found TRUE)
            endif()
        endforeach()
        if(NOT _found)
            list(APPEND _missing "${_name}")
        endif()
    endforeach()
    if(_count EQUAL 0)
        message(FATAL_ERROR "No CPMAddPackage(NAME ...) found in ${HIO_DEPS_FILE}.")
    endif()
    if(_missing)
        message(FATAL_ERROR
            "CPM dependencies without a licence row in packaging/common/licenses.cmake: ${_missing}. "
            "Add a row to HIO_THIRD_PARTY and the licence text to packaging/common/licenses/.")
    endif()
    message(STATUS "Licence table covers all ${_count} CPM dependencies.")
    return()
endif()

include_guard(GLOBAL)

function(hio_install_licenses destination)
    set(_src "${PROJECT_SOURCE_DIR}")
    set(_out "${PROJECT_BINARY_DIR}/licenses")
    file(MAKE_DIRECTORY "${_out}")

    # Qt-SOURCE.txt: the exact Qt version this build uses (written offer, ADR-0119).
    set(_qt "${Qt6_VERSION}")
    string(REGEX MATCH "^[0-9]+\\.[0-9]+" _qt_minor "${_qt}")
    file(WRITE "${_out}/Qt-SOURCE.txt"
"HARDWARIO Monitor uses the Qt libraries, version ${_qt}, under the GNU Lesser General Public
License version 3 (Qt-LGPL-3.0.txt; it refers to GPL-3.0.txt). Qt is linked dynamically: the Qt
libraries are separate files in this installation and can be replaced by a modified version
(see LICENSE for the rights the LGPL gives you).

The complete corresponding source code of this Qt version is available at
  https://download.qt.io/archive/qt/${_qt_minor}/${_qt}/single/qt-everywhere-src-${_qt}.tar.xz
  (all Qt ${_qt} sources: https://download.qt.io/archive/qt/${_qt_minor}/${_qt}/submodules/)

Written offer: for at least three years after HARDWARIO a.s. last distributes this version of
HARDWARIO Monitor, HARDWARIO a.s. will give anyone who asks a copy of that source code on a common
data carrier for no more than the cost of making and sending the copy. Contact: HARDWARIO a.s.,
https://www.hardwario.com/.
")

    # THIRD-PARTY-NOTICES.txt: the index of the shipped components.
    set(_text "HARDWARIO Monitor ${HIO_VERSION} - third-party components\n")
    string(APPEND _text "The licence texts are in this directory (the file names below).\n\n")
    foreach(_row IN LISTS HIO_THIRD_PARTY)
        string(REPLACE "@QT_VERSION@" "${_qt}" _row "${_row}")
        string(REPLACE "|" ";" _fields "${_row}")
        list(GET _fields 0 _name)
        list(GET _fields 1 _version)
        list(GET _fields 2 _licence)
        list(GET _fields 3 _url)
        list(GET _fields 4 _files)
        list(GET _fields 5 _distributed)
        if(NOT _distributed STREQUAL "YES")
            continue()
        endif()
        string(APPEND _text "${_name} ${_version}\n  Licence: ${_licence}\n  ${_url}\n"
                            "  Text: ${_files}\n\n")
    endforeach()
    file(WRITE "${_out}/THIRD-PARTY-NOTICES.txt" "${_text}")

    install(FILES
        "${_src}/LICENSE"
        "${_src}/packaging/common/licenses/Qt-LGPL-3.0.txt"
        "${_src}/packaging/common/licenses/GPL-3.0.txt"
        "${_src}/packaging/common/licenses/CLI11-BSD-3-Clause.txt"
        "${_src}/packaging/common/licenses/replxx-LICENSE.md"
        "${_src}/packaging/common/licenses/miniz-MIT.txt"
        "${_src}/app/gui/assets/fonts/Inter-LICENSE.txt"
        "${_src}/app/gui/assets/fonts/JetBrainsMono-OFL.txt"
        "${_out}/Qt-SOURCE.txt"
        "${_out}/THIRD-PARTY-NOTICES.txt"
        DESTINATION "${destination}")
    install(FILES "${_src}/app/gui/assets/icons/LICENSE-Apache-2.0.txt"
        DESTINATION "${destination}"
        RENAME "MaterialSymbols-Apache-2.0.txt")
endfunction()
