#pragma once

// Linux USB access to J-Link probes (ADR-0072,
// plan/features/0007-m4-wizard-framework-diagnostics.md).
//
// For every USB device in <sysfsRoot>/bus/usb/devices/* with idVendor 1366 (SEGGER) the device
// node <devRoot>/bus/usb/<busnum>/<devnum> is checked for read and write access by this process.
// No vendor calls. On other operating systems the list is always empty (nothing to check).
//
// Part of the small target hwio::diag_usb (no dependency on libs/device), so that DeviceSession can
// report usb.access_denied after a failed probe open without a dependency cycle.
//
// Bounds: at most maxUsbEntries directory entries are inspected; each sysfs file read is at most
// 256 bytes.

#include "hwio/core/error.h"
#include "hwio/core/result.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hwio::diag {

inline constexpr std::string_view seggerUsbVendorId = "1366";
inline constexpr std::size_t maxUsbEntries = 1024;

// The udev rule and the commands shown by the setup wizard and the troubleshooting topic
// (ADR-0072). ADR-0185: `uaccess` only (an ACL for the user of the local seat); no MODE="0666".
inline constexpr std::string_view jlinkUdevRule =
    R"(SUBSYSTEM=="usb", ATTR{idVendor}=="1366", TAG+="uaccess")";
// ADR-0185: the same name as the packaged rule; a `99-*` file would run after
// 73-seat-late.rules and `uaccess` would grant nothing.
inline constexpr std::string_view jlinkUdevRuleFile = "/etc/udev/rules.d/60-hardwario-jlink.rules";
// The rule file of older instructions and of the SEGGER installer (MODE="0666").
inline constexpr std::string_view oldJLinkUdevRuleFile = "/etc/udev/rules.d/99-jlink.rules";
// Install the rule, remove an old 99-jlink.rules that has MODE="0666", reload udev.
[[nodiscard]] std::vector<std::string> jlinkUdevCommands();

struct UsbDeviceAccess {
    int bus = 0;
    int device = 0;
    std::string product; // sysfs "product" ("" when absent)
    bool accessible = false;
};

// Errors: io.read_failed when <sysfsRoot>/bus/usb/devices cannot be listed (e.g. a sandbox).
[[nodiscard]] Result<std::vector<UsbDeviceAccess>>
checkUsbAccess(const std::filesystem::path& sysfsRoot = "/sys",
               const std::filesystem::path& devRoot = "/dev");

// usb.access_denied (detail: the inaccessible bus/device nodes) when one of `devices` is not
// accessible, nullopt otherwise.
[[nodiscard]] std::optional<Error> usbAccessDeniedError(std::span<const UsbDeviceAccess> devices);

} // namespace hwio::diag
