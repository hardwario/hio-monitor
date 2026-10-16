#pragma once

// Linux USB access to J-Link probes (ADR-0072,
// plan/features/0007-m4-wizard-framework-diagnostics.md).
//
// For every USB device in <sysfsRoot>/bus/usb/devices/* with idVendor 1366 (SEGGER) the device
// node <devRoot>/bus/usb/<busnum>/<devnum> is checked for read and write access by this process.
// No vendor calls. On other operating systems the list is always empty (nothing to check).
//
// Part of the small target hio::diag_usb (no dependency on libs/device), so that DeviceSession can
// report usb.access_denied after a failed probe open without a dependency cycle.
//
// Bounds: at most maxUsbEntries directory entries are inspected; each sysfs file read is at most
// 256 bytes.

#include "hio/core/error.h"
#include "hio/core/result.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hio::diag {

inline constexpr std::string_view seggerUsbVendorId = "1366";
inline constexpr std::size_t maxUsbEntries = 1024;

// The udev rule and the commands shown by the setup wizard and the troubleshooting topic
// (ADR-0072).
inline constexpr std::string_view jlinkUdevRule =
    R"(SUBSYSTEM=="usb", ATTR{idVendor}=="1366", MODE="0666", TAG+="uaccess")";
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

} // namespace hio::diag
