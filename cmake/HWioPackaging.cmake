# Install tree, deployment and packages (plan/features/0015, ADR-0117 - ADR-0126).
#
# HWIO_PACKAGE_KIND (cache, default "dev") says how the build is distributed; it is compiled into
# hwio/core/package_kind.h (BuildInfo::packageKind, `hwio --version --json` "package", the
# diagnostic report, Settings > About):
#   dev           every normal build; `cmake --install` gives the plain layout below
#   appimage      Linux AppImage (packaging/linux/build-appimage.sh), install prefix = AppDir
#   deb           Linux .deb (CPack DEB), files under /opt/hardwario-monitor (ADR-0121)
#   macos-bundle  macOS "HARDWARIO Monitor.app" in a DMG (ADR-0120, ADR-0126)
#   msi           Windows per-machine MSI (CPack WIX, ADR-0117)
# The *-package presets set it; the Linux package job configures the same build tree once for
# "deb" and once for "appimage" (only version.cpp is rebuilt, ADR-0121: one Linux build feeds both).
#
# Installed layout (relative to the install prefix):
#   Linux    <app>/bin/{hwio-monitor,hwio,qt.conf}, <app>/lib (Qt), <app>/plugins, <app>/qml,
#            <app>/translations, <app>/licenses; <app> = "" (dev), "usr" (appimage, prefix =
#            AppDir), "opt/hardwario-monitor" (deb, prefix = /). RPATH $ORIGIN/../lib.
#            deb and appimage also: usr/share/{applications,metainfo,icons/hicolor}; deb also
#            usr/lib/udev/rules.d/60-hardwario-jlink.rules and the usr/bin/{hwio-monitor,hwio} links.
#   macOS    "HARDWARIO Monitor.app"/Contents/{MacOS/{hwio-monitor,hwio}, Frameworks, PlugIns,
#            Resources/{qml,translations,licenses,hwio-monitor.icns}}
#   Windows  hwio-monitor.exe, hwio.exe, Qt DLLs, plugins/, qml/, translations/, licenses/, MSVC
#            runtime DLLs (app-local), qt.conf
#
# Deployment: qt_generate_deploy_script() runs cmake/HWioDeploy.cmake.in at install time. Linux
# copies an explicit allow-list of Qt plugins and QML modules (the build keeps
# QT_QML_MODULE_NO_IMPORT_SCAN ON, so qt_generate_deploy_qml_app_script() has no import list) and
# the Qt libraries they need (file(GET_RUNTIME_DEPENDENCIES), Qt's lib directory only); macOS uses
# macdeployqt and Windows windeployqt through qt_deploy_runtime_dependencies() (both scan
# app/gui/qml themselves). Only the Czech Qt catalogs are deployed (ADR-0027).
#
# Functions:
#   hwio_generate_package_kind_header()  before the libraries (generated/hwio/core/package_kind.h)
#   hwio_add_packaging()                 at the end of the top-level CMakeLists.txt: install rules,
#                                       deploy script, CPack, installed-tree tests
include_guard(GLOBAL)

set(HWIO_PACKAGE_KINDS dev appimage deb macos-bundle msi)
set(HWIO_PACKAGE_KIND "dev" CACHE STRING "How this build is distributed: ${HWIO_PACKAGE_KINDS}")
set_property(CACHE HWIO_PACKAGE_KIND PROPERTY STRINGS ${HWIO_PACKAGE_KINDS})
if(NOT HWIO_PACKAGE_KIND IN_LIST HWIO_PACKAGE_KINDS)
    message(FATAL_ERROR
        "HWIO_PACKAGE_KIND '${HWIO_PACKAGE_KIND}' is not one of: ${HWIO_PACKAGE_KINDS}.")
endif()
if((HWIO_PACKAGE_KIND MATCHES "^(appimage|deb)$" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
   OR (HWIO_PACKAGE_KIND STREQUAL "macos-bundle" AND NOT APPLE)
   OR (HWIO_PACKAGE_KIND STREQUAL "msi" AND NOT WIN32))
    message(FATAL_ERROR
        "HWIO_PACKAGE_KIND '${HWIO_PACKAGE_KIND}' does not match this OS (${CMAKE_SYSTEM_NAME}).")
endif()

# Application id (ADR-0018): desktop file, metainfo, icon name, AppUserModelID.
set(HWIO_APP_ID "com.hardwario.monitor")
set(HWIO_PRODUCT_NAME "HARDWARIO Monitor")
set(HWIO_VENDOR "HARDWARIO a.s.")
set(HWIO_MACOS_BUNDLE_DIR "HARDWARIO Monitor.app")

# Linux: the application directory inside the install prefix.
if(HWIO_PACKAGE_KIND STREQUAL "deb")
    set(HWIO_INSTALL_APPDIR "opt/hardwario-monitor")
elseif(HWIO_PACKAGE_KIND STREQUAL "appimage")
    set(HWIO_INSTALL_APPDIR "usr")
else()
    set(HWIO_INSTALL_APPDIR "")
endif()

# Install-relative directories per OS (also used by the GUI's LicensesModel through
# hwio/core/package_kind.h - keep them in sync with app/gui/src/install_paths.cpp).
if(APPLE)
    set(HWIO_INSTALL_BINDIR "${HWIO_MACOS_BUNDLE_DIR}/Contents/MacOS")
    set(HWIO_INSTALL_LICENSEDIR "${HWIO_MACOS_BUNDLE_DIR}/Contents/Resources/licenses")
elseif(WIN32)
    set(HWIO_INSTALL_BINDIR ".")
    set(HWIO_INSTALL_LICENSEDIR "licenses")
else()
    if(HWIO_INSTALL_APPDIR)
        set(_hwio_app "${HWIO_INSTALL_APPDIR}/")
    else()
        set(_hwio_app "")
    endif()
    set(HWIO_INSTALL_BINDIR "${_hwio_app}bin")
    set(HWIO_INSTALL_LIBDIR "${_hwio_app}lib")
    set(HWIO_INSTALL_PLUGINSDIR "${_hwio_app}plugins")
    set(HWIO_INSTALL_QMLDIR "${_hwio_app}qml")
    set(HWIO_INSTALL_TRANSLATIONSDIR "${_hwio_app}translations")
    set(HWIO_INSTALL_LICENSEDIR "${_hwio_app}licenses")
    unset(_hwio_app)
endif()

# Qt plugins deployed on Linux (relative to Qt's plugin directory); everything else that Qt's
# deploy API would take for the linked modules (qmltooling, generic, egldeviceintegrations,
# platformthemes, networkinformation, other platforms, ...) is left out.
set(HWIO_LINUX_QT_PLUGINS
    platforms/libqxcb.so
    platforms/libqwayland-egl.so
    platforms/libqwayland-generic.so
    platforms/libqoffscreen.so
    xcbglintegrations/libqxcb-glx-integration.so
    xcbglintegrations/libqxcb-egl-integration.so
    wayland-shell-integration/libxdg-shell.so
    wayland-decoration-client/libbradient.so
    wayland-graphics-integration-client/libqt-plugin-wayland-egl.so
    platforminputcontexts/libcomposeplatforminputcontextplugin.so
    platforminputcontexts/libibusplatforminputcontextplugin.so
    imageformats/libqsvg.so
    imageformats/libqjpeg.so
    imageformats/libqico.so
    iconengines/libqsvgicon.so
    tls/libqopensslbackend.so
    tls/libqcertonlybackend.so)

# QML modules deployed on Linux (directories below Qt's qml directory): the imports of app/gui/qml
# and their dependencies (QtQuick.Controls with the Material style and its Basic fallback; the
# Controls' optional imports of the other styles are left out).
set(HWIO_LINUX_QML_MODULES
    QML
    QtQml
    QtQml/Models
    QtQml/WorkerScript
    QtQuick
    QtQuick/Window
    QtQuick/Layouts
    QtQuick/Templates
    QtQuick/Shapes
    QtQuick/Effects
    QtQuick/Controls
    QtQuick/Controls/impl
    QtQuick/Controls/Basic
    QtQuick/Controls/Basic/impl
    QtQuick/Controls/Material
    QtQuick/Controls/Material/impl
    QtQuick/Dialogs
    QtQuick/Dialogs/quickimpl
    Qt/labs/folderlistmodel)

# Qt translation catalogs deployed for each UI language other than English (ADR-0027); missing
# catalogs (e.g. qtdeclarative_cs in some Qt versions) are skipped.
set(HWIO_QT_CATALOGS qtbase qtdeclarative qtconnectivity)
set(HWIO_QT_LOCALES cs)

function(hwio_generate_package_kind_header)
    configure_file(
        "${PROJECT_SOURCE_DIR}/cmake/package_kind.h.in"
        "${PROJECT_BINARY_DIR}/generated/hwio/core/package_kind.h"
        @ONLY)
endfunction()

# ---- Linux desktop integration (deb, appimage) ----------------------------------------------------
function(_hwio_install_linux_desktop_files)
    set(_linux "${PROJECT_SOURCE_DIR}/packaging/linux")
    set(_icons "${PROJECT_SOURCE_DIR}/packaging/common/icons")
    string(TIMESTAMP _date "%Y-%m-%d" UTC)
    set(HWIO_RELEASE_DATE "${_date}")
    configure_file("${_linux}/${HWIO_APP_ID}.metainfo.xml.in"
        "${PROJECT_BINARY_DIR}/packaging/${HWIO_APP_ID}.metainfo.xml" @ONLY)
    install(FILES "${_linux}/${HWIO_APP_ID}.desktop" DESTINATION "usr/share/applications")
    install(FILES "${PROJECT_BINARY_DIR}/packaging/${HWIO_APP_ID}.metainfo.xml"
        DESTINATION "usr/share/metainfo")
    foreach(_size IN ITEMS 16 24 32 48 64 128 256 512)
        install(FILES "${_icons}/hwio-monitor-${_size}.png"
            DESTINATION "usr/share/icons/hicolor/${_size}x${_size}/apps"
            RENAME "${HWIO_APP_ID}.png")
    endforeach()
endfunction()

# ---- the deploy script -----------------------------------------------------------------------------
# cmake/HWioDeploy.cmake.in (configured with @ONLY) is included by Qt's generated deploy script, so
# it runs at install time with Qt's deploy API (qt_deploy_runtime_dependencies & co.).
function(_hwio_add_deploy_script)
    list(JOIN HWIO_LINUX_QT_PLUGINS " " HWIO_LINUX_QT_PLUGINS)
    list(JOIN HWIO_LINUX_QML_MODULES " " HWIO_LINUX_QML_MODULES)
    set(HWIO_DEPLOY_QML_SOURCE "${PROJECT_SOURCE_DIR}/app/gui/qml")
    set(HWIO_DEPLOY_QT_PLUGINS_DIR "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}")
    set(HWIO_DEPLOY_QT_LIB_DIR "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_LIBS}")
    set(HWIO_DEPLOY_QT_QML_DIR "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_QML}")
    set(HWIO_DEPLOY_QT_TRANSLATIONS_DIR "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}")
    if(APPLE)
        set(HWIO_DEPLOY_OS "macos")
    elseif(WIN32)
        set(HWIO_DEPLOY_OS "windows")
    else()
        set(HWIO_DEPLOY_OS "linux")
    endif()
    # Qt's deploy API walks hwio-monitor's dependencies from this (top-level) directory, so the GUI
    # Qt modules found in app/gui must be known here too.
    find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Qml Quick QuickControls2)
    set(_script "${PROJECT_BINARY_DIR}/packaging/hwio_deploy.cmake")
    # @VARIABLES@ first, then the generator expressions ($<TARGET_FILE:...>).
    configure_file("${PROJECT_SOURCE_DIR}/cmake/HWioDeploy.cmake.in" "${_script}.in" @ONLY)
    file(GENERATE OUTPUT "${_script}" INPUT "${_script}.in")
    qt_generate_deploy_script(
        TARGET hwio-monitor
        OUTPUT_SCRIPT _hwio_deploy_script
        CONTENT "include(\"${_script}\")")
    install(SCRIPT "${_hwio_deploy_script}")
endfunction()

# ---- Windows: MSVC runtime app-local ------------------------------------------------------------
function(_hwio_install_msvc_runtime)
    # clang-cl links the MSVC STL; the redistributable DLLs come from the VS Build Tools
    # (vcvars sets VCToolsRedistDir). The MSI does not install the VC++ redistributable.
    # Forward slashes: the paths end up in cmake_install.cmake, where "\P" is an escape sequence.
    file(TO_CMAKE_PATH "$ENV{VCToolsRedistDir}" _redist)
    if(NOT _redist)
        message(FATAL_ERROR
            "HWIO_PACKAGE_KIND=msi needs the MSVC redistributable DLLs: configure from a VS developer "
            "prompt (VCToolsRedistDir is not set).")
    endif()
    file(GLOB _crt_dirs LIST_DIRECTORIES true "${_redist}/x64/Microsoft.VC*.CRT")
    list(SORT _crt_dirs)
    list(POP_BACK _crt_dirs _crt)
    if(NOT _crt)
        message(FATAL_ERROR "No Microsoft.VC*.CRT directory in ${_redist}/x64.")
    endif()
    file(GLOB _dlls "${_crt}/vcruntime140*.dll" "${_crt}/msvcp140*.dll")
    if(NOT _dlls)
        message(FATAL_ERROR "No MSVC runtime DLLs in ${_crt}.")
    endif()
    message(STATUS "MSVC runtime (app-local): ${_crt}")
    install(FILES ${_dlls} DESTINATION ".")
endfunction()

# ---- installed-tree tests --------------------------------------------------------------------------
function(_hwio_add_installed_tree_tests)
    # Not under ThreadSanitizer: the installed GUI runs Qt's uninstrumented threads (QML engine,
    # scene graph), which TSan reports (exit code 66), as documented for the network tests in
    # libs/firmware/tests/CMakeLists.txt. Every other preset runs it.
    if("thread" IN_LIST HWIO_SANITIZERS)
        return()
    endif()
    set(_prefix "${PROJECT_BINARY_DIR}/installed-tree")
    if(APPLE)
        set(_bin "${_prefix}/${HWIO_INSTALL_BINDIR}")
    elseif(WIN32)
        set(_bin "${_prefix}")
    else()
        set(_bin "${_prefix}/${HWIO_INSTALL_BINDIR}")
    endif()
    add_test(NAME Packaging.InstalledTreeSelfTest
        COMMAND "${CMAKE_COMMAND}"
            "-DHWIO_BUILD_DIR=${PROJECT_BINARY_DIR}"
            "-DHWIO_PREFIX=${_prefix}"
            "-DHWIO_BIN_DIR=${_bin}"
            "-DHWIO_PACKAGE_KIND=${HWIO_PACKAGE_KIND}"
            "-DHWIO_CONFIG=$<CONFIG>"
            -P "${PROJECT_SOURCE_DIR}/cmake/HWioInstalledTreeTest.cmake")
    set_tests_properties(Packaging.InstalledTreeSelfTest PROPERTIES
        LABELS "package"
        TIMEOUT 300
        RUN_SERIAL TRUE)
    if(APPLE)
        # ADR-0126 / plan 0015 D2: the installed bundle signed ad hoc, inside-out, with the
        # hardened runtime; the entitlements of both binaries are checked (sign-and-notarize.sh).
        set_tests_properties(Packaging.InstalledTreeSelfTest PROPERTIES
            FIXTURES_SETUP hwio_installed_tree)
        add_test(NAME Packaging.MacosSignatureAndEntitlements
            COMMAND "${PROJECT_SOURCE_DIR}/packaging/macos/sign-and-notarize.sh" sign
                "${_prefix}/${HWIO_MACOS_BUNDLE_DIR}" -)
        set_tests_properties(Packaging.MacosSignatureAndEntitlements PROPERTIES
            LABELS "package"
            FIXTURES_REQUIRED hwio_installed_tree
            RUN_SERIAL TRUE)
    endif()
    add_test(NAME Packaging.LicenceTableCoversCpmDependencies
        COMMAND "${CMAKE_COMMAND}"
            "-DHWIO_DEPS_FILE=${PROJECT_SOURCE_DIR}/cmake/HWioDeps.cmake"
            -P "${PROJECT_SOURCE_DIR}/packaging/common/licenses.cmake")
    set_tests_properties(Packaging.LicenceTableCoversCpmDependencies PROPERTIES LABELS "package")
endfunction()

# ---- CPack -----------------------------------------------------------------------------------------
function(_hwio_configure_cpack)
    set(CPACK_PACKAGE_NAME "hwio-monitor")
    set(CPACK_PACKAGE_VENDOR "${HWIO_VENDOR}")
    set(CPACK_PACKAGE_VERSION "${HWIO_VERSION_NUMERIC}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "Console, log and flashing tool for HARDWARIO devices")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/hardwario/hio-monitor")
    set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
    set(CPACK_PACKAGE_DIRECTORY "${PROJECT_BINARY_DIR}/packages")
    set(CPACK_STRIP_FILES OFF) # symbols are split by the release workflow (ADR-0123)
    # The version in artifact names (ADR-0125): the tag's version; build metadata with '.'.
    string(REPLACE "+" "." HWIO_VERSION_FILE_NAME "${HWIO_VERSION}")

    if(HWIO_PACKAGE_KIND STREQUAL "deb")
        include("${PROJECT_SOURCE_DIR}/packaging/linux/deb/cpack-deb.cmake")
    elseif(HWIO_PACKAGE_KIND STREQUAL "msi")
        include("${PROJECT_SOURCE_DIR}/packaging/windows/wix/cpack-wix.cmake")
    else()
        # dev / appimage / macos-bundle are not built by CPack (AppImage: build-appimage.sh,
        # DMG: packaging/macos/create-dmg.sh); a TGZ of the install tree is the fallback.
        set(CPACK_GENERATOR "TGZ")
    endif()
    include(CPack)
endfunction()

function(hwio_add_packaging)
    # Executables.
    if(APPLE)
        # The bundle directory gets its user-visible name at install; CFBundleExecutable stays
        # hwio-monitor.
        install(DIRECTORY "$<TARGET_BUNDLE_DIR:hwio-monitor>/"
            DESTINATION "${HWIO_MACOS_BUNDLE_DIR}"
            USE_SOURCE_PERMISSIONS)
        set_target_properties(hwio PROPERTIES INSTALL_RPATH "@executable_path/../Frameworks")
        install(TARGETS hwio RUNTIME DESTINATION "${HWIO_INSTALL_BINDIR}")
    elseif(WIN32)
        install(TARGETS hwio-monitor hwio RUNTIME DESTINATION ".")
        if(HWIO_PACKAGE_KIND STREQUAL "msi")
            _hwio_install_msvc_runtime()
        endif()
    else()
        set_target_properties(hwio-monitor hwio PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
        install(TARGETS hwio-monitor hwio RUNTIME DESTINATION "${HWIO_INSTALL_BINDIR}")
        if(HWIO_PACKAGE_KIND MATCHES "^(deb|appimage)$")
            _hwio_install_linux_desktop_files()
        endif()
        if(HWIO_PACKAGE_KIND STREQUAL "deb")
            # ADR-0072 / ADR-0121: J-Link USB access for the logged-in user.
            install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/60-hardwario-jlink.rules"
                DESTINATION "usr/lib/udev/rules.d")
            # ADR-0122: hwio-monitor and hwio on PATH (absolute links into /opt, Debian policy for
            # links between top-level directories).
            file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/packaging/usr-bin")
            foreach(_exe IN ITEMS hwio-monitor hwio)
                file(CREATE_LINK "/${HWIO_INSTALL_APPDIR}/bin/${_exe}"
                    "${PROJECT_BINARY_DIR}/packaging/usr-bin/${_exe}" SYMBOLIC)
                install(FILES "${PROJECT_BINARY_DIR}/packaging/usr-bin/${_exe}"
                    DESTINATION "usr/bin")
            endforeach()
        endif()
    endif()

    include("${PROJECT_SOURCE_DIR}/packaging/common/licenses.cmake")
    hwio_install_licenses("${HWIO_INSTALL_LICENSEDIR}")

    _hwio_add_deploy_script()
    _hwio_configure_cpack()

    if(HWIO_BUILD_TESTS)
        _hwio_add_installed_tree_tests()
    endif()
endfunction()
