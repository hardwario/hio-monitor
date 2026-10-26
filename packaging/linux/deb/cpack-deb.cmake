# CPack DEB configuration of hardwario-monitor (plan/features/0015 C2, ADR-0121, ADR-0122).
# Included by cmake/HioPackaging.cmake when HIO_PACKAGE_KIND=deb.
#
# Files: /opt/hardwario-monitor/{bin,lib,plugins,qml,translations,licenses} (private Qt), the links
# /usr/bin/hio-monitor and /usr/bin/hio, desktop file, AppStream metainfo and hicolor icons under
# /usr/share, the J-Link udev rule under /usr/lib/udev/rules.d. postinst/postrm reload udev.
# Requires Ubuntu 24.04+ / Debian 13+ (ADR-0118).

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGING_INSTALL_PREFIX "/")
set(CPACK_DEBIAN_PACKAGE_NAME "hardwario-monitor")
# Debian version: the first '-' (pre-release) becomes '~' so that 2.0.0~rc.1 < 2.0.0.
string(REGEX REPLACE "^([0-9]+\\.[0-9]+\\.[0-9]+)-" "\\1~" _hio_deb_version "${HIO_VERSION}")
set(CPACK_DEBIAN_PACKAGE_VERSION "${_hio_deb_version}")
set(CPACK_DEBIAN_PACKAGE_RELEASE "")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
# ADR-0125 artifact name: hardwario-monitor_<version>_amd64.deb (<version> as the tag, without "v").
set(CPACK_DEBIAN_FILE_NAME "hardwario-monitor_${HIO_VERSION_FILE_NAME}_amd64.deb")
# PLACEHOLDER (packaging/RELEASE.md release gate): HARDWARIO supplies the support e-mail address.
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "HARDWARIO a.s. <support-email-pending@hardwario.invalid>")
set(CPACK_DEBIAN_PACKAGE_SECTION "electronics")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/hardwario/hio-monitor")
# The synopsis is CPACK_PACKAGE_DESCRIPTION_SUMMARY; this is the extended description.
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
"HARDWARIO Monitor connects to HARDWARIO devices such as CHESTER through a
SEGGER J-Link probe or Bluetooth: device console and logs, application and
modem firmware flashing, connection diagnostics. Includes the command-line
tool hio. SEGGER J-Link Software 8.00 or newer must be installed separately.")
# dpkg-shlibdeps computes the system libraries of the binaries, the Qt plugins and QML modules;
# the private Qt in /opt/hardwario-monitor/lib is resolved there and not turned into dependencies.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# dpkg-shlibdeps runs in the staging directory; the private Qt is resolved there (-l), which keeps
# it out of the dependencies.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS "opt/hardwario-monitor/lib")
# bluez explicitly (D-Bus service, not a linked library). libstdc++6 comes from dpkg-shlibdeps
# (">= 13.1" for the libstdc++ 13 build, ADR-0121 / ADR-0177); listing it twice is a lintian
# error (package-depends-on-multiple-libstdc-versions).
set(CPACK_DEBIAN_PACKAGE_DEPENDS "bluez")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${PROJECT_SOURCE_DIR}/packaging/linux/deb/postinst"
    "${PROJECT_SOURCE_DIR}/packaging/linux/deb/postrm")
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)
set(CPACK_DEBIAN_COMPRESSION_TYPE "xz")

# Debian policy files: copyright, a changelog and the lintian overrides.
set(_hio_deb_doc "${PROJECT_BINARY_DIR}/packaging/deb")
file(MAKE_DIRECTORY "${_hio_deb_doc}")
set(QT_VERSION "${Qt6_VERSION}")
configure_file("${PROJECT_SOURCE_DIR}/packaging/linux/deb/copyright.in"
    "${_hio_deb_doc}/copyright" @ONLY)
string(TIMESTAMP _hio_deb_date "%a, %d %b %Y %H:%M:%S +0000" UTC)
file(WRITE "${_hio_deb_doc}/changelog"
"hardwario-monitor (${_hio_deb_version}) unstable; urgency=medium

  * HARDWARIO Monitor ${HIO_VERSION}, see
    https://github.com/hardwario/hio-monitor/releases

 -- ${CPACK_DEBIAN_PACKAGE_MAINTAINER}  ${_hio_deb_date}
")
add_custom_command(
    OUTPUT "${_hio_deb_doc}/changelog.gz"
    COMMAND gzip -9nkf "${_hio_deb_doc}/changelog"
    DEPENDS "${_hio_deb_doc}/changelog"
    VERBATIM)
add_custom_target(hio_deb_changelog ALL DEPENDS "${_hio_deb_doc}/changelog.gz")
install(FILES "${_hio_deb_doc}/copyright" "${_hio_deb_doc}/changelog.gz"
    DESTINATION "usr/share/doc/hardwario-monitor")
# Manual pages of the two commands on PATH.
foreach(_hio_page IN ITEMS hio hio-monitor)
    configure_file("${PROJECT_SOURCE_DIR}/packaging/linux/deb/${_hio_page}.1.in"
        "${_hio_deb_doc}/${_hio_page}.1" @ONLY)
    add_custom_command(
        OUTPUT "${_hio_deb_doc}/${_hio_page}.1.gz"
        COMMAND gzip -9nkf "${_hio_deb_doc}/${_hio_page}.1"
        DEPENDS "${_hio_deb_doc}/${_hio_page}.1"
        VERBATIM)
    list(APPEND _hio_deb_pages "${_hio_deb_doc}/${_hio_page}.1.gz")
endforeach()
add_custom_target(hio_deb_man_pages ALL DEPENDS ${_hio_deb_pages})
install(FILES ${_hio_deb_pages} DESTINATION "usr/share/man/man1")
# Debian policy: binaries and libraries are stripped (the release workflow keeps the symbols of
# the build tree, ADR-0123). Also the ICU libraries that come with Qt.
set(CPACK_STRIP_FILES ON)
set(CPACK_PRE_BUILD_SCRIPTS "${PROJECT_SOURCE_DIR}/packaging/linux/deb/strip-staged.cmake")
install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/deb/lintian-overrides"
    DESTINATION "usr/share/lintian/overrides"
    RENAME "hardwario-monitor")
