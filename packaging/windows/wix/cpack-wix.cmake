# CPack WIX configuration of the Windows MSI (plan/features/0015 E1, ADR-0117, ADR-0122).
# Included by cmake/HioPackaging.cmake when HIO_PACKAGE_KIND=msi.
#
# WiX 4 or newer through CPack's WIX generator (CPACK_WIX_VERSION 4, CMake >= 3.30), per-machine,
# %ProgramFiles%\HARDWARIO\Monitor, fixed UpgradeCode (every release is a major upgrade; downgrades
# are refused with "A newer version of HARDWARIO Monitor is already installed."), LICENSE as RTF,
# the product icon. Own parts (packaging/windows/wix/):
#   WIX.template.in   CMake's v4 template with our MajorUpgrade message, the feature tree UI and
#                     the features below
#   features.wxs      Start-menu shortcut "HARDWARIO Monitor" with System.AppUserModel.ID
#                     com.hardwario.monitor (ADR-0018), the feature AddToPath (on: the install
#                     directory on the system PATH, ADR-0122), the feature DesktopShortcut (off;
#                     on with ADDDESKTOPSHORTCUT=1, e.g. msiexec /i ... /qn ADDDESKTOPSHORTCUT=1)
# The MSI neither contains nor runs the SEGGER J-Link installer (ADR-0006).

if(CMAKE_VERSION VERSION_LESS 3.30)
    message(FATAL_ERROR "The MSI (CPack WIX with WiX >= 4) needs CMake 3.30 or newer.")
endif()

set(CPACK_GENERATOR "WIX")
set(CPACK_WIX_VERSION 4)
set(CPACK_PACKAGE_NAME "HARDWARIO Monitor")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "HARDWARIO/Monitor")
set(CPACK_PACKAGE_FILE_NAME "hio-monitor-${HIO_VERSION_FILE_NAME}-windows-x64")
set(CPACK_WIX_UPGRADE_GUID "D58A5657-E91D-4245-9431-FC43738716ED")
set(CPACK_WIX_INSTALL_SCOPE "perMachine")
set(CPACK_WIX_ARCHITECTURE "x64")
set(CPACK_WIX_PRODUCT_ICON "${PROJECT_SOURCE_DIR}/packaging/common/icons/hio-monitor.ico")
set(CPACK_WIX_PROGRAM_MENU_FOLDER ".")
set(CPACK_WIX_UI_REF "WixUI_FeatureTree")
set(CPACK_WIX_TEMPLATE "${PROJECT_SOURCE_DIR}/packaging/windows/wix/WIX.template.in")
set(CPACK_WIX_EXTRA_SOURCES "${PROJECT_SOURCE_DIR}/packaging/windows/wix/features.wxs")
set(CPACK_WIX_EXTENSIONS "WixToolset.UI.wixext")
set(CPACK_WIX_PROPERTY_ARPURLINFOABOUT "https://www.hardwario.com/")
set(CPACK_WIX_PROPERTY_ARPHELPLINK "https://github.com/hardwario/hio-monitor")
set(CPACK_WIX_PROPERTY_ARPCONTACT "HARDWARIO a.s.")
# The licence as RTF: CPack converts a .txt licence (LICENSE has no extension).
configure_file("${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_BINARY_DIR}/packaging/LICENSE.txt" COPYONLY)
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_BINARY_DIR}/packaging/LICENSE.txt")
