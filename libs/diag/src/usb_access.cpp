#include "hio/diag/usb_access.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"

#include <QString>
#include <QtSystemDetection>

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef Q_OS_LINUX
#include <unistd.h>
#endif

namespace hio::diag {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t maxAttributeBytes = 256;

// The first line of a small sysfs attribute, trimmed; nullopt when it cannot be read.
std::optional<std::string> readAttribute(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::array<char, maxAttributeBytes> buffer{};
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    std::string_view text(buffer.data(), static_cast<std::size_t>(file.gcount()));
    if (const auto newline = text.find('\n'); newline != std::string_view::npos) {
        text = text.substr(0, newline);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\r' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return std::string(text);
}

std::optional<int> readNumber(const fs::path& path) {
    const auto text = readAttribute(path);
    if (!text) {
        return std::nullopt;
    }
    bool ok = false;
    const int value = QString::fromStdString(*text).toInt(&ok);
    if (!ok) {
        return std::nullopt;
    }
    return value;
}

bool canReadAndWrite([[maybe_unused]] const fs::path& node) {
#ifdef Q_OS_LINUX
    return ::access(node.c_str(), R_OK | W_OK) == 0;
#else
    return true;
#endif
}

} // namespace

std::vector<std::string> jlinkUdevCommands() {
    return {std::format("echo '{}' | sudo tee /etc/udev/rules.d/99-jlink.rules", jlinkUdevRule),
            "sudo udevadm control --reload-rules && sudo udevadm trigger"};
}

Result<std::vector<UsbDeviceAccess>> checkUsbAccess([[maybe_unused]] const fs::path& sysfsRoot,
                                                    [[maybe_unused]] const fs::path& devRoot) {
    std::vector<UsbDeviceAccess> devices;
#ifdef Q_OS_LINUX
    const fs::path directory = sysfsRoot / "bus" / "usb" / "devices";
    std::error_code error;
    fs::directory_iterator it(directory, error);
    if (error) {
        return std::unexpected(makeError(
            ErrorCode::IoReadFailed, std::format("{}: {}", toUtf8(directory), error.message())));
    }
    std::size_t inspected = 0;
    for (; it != fs::directory_iterator() && inspected < maxUsbEntries; it.increment(error)) {
        if (error) {
            break;
        }
        ++inspected;
        const fs::path entry = it->path();
        const auto vendor = readAttribute(entry / "idVendor");
        if (!vendor || *vendor != seggerUsbVendorId) {
            continue;
        }
        const auto bus = readNumber(entry / "busnum");
        const auto number = readNumber(entry / "devnum");
        if (!bus || !number) {
            continue; // an interface entry, not a device
        }
        const fs::path node =
            devRoot / "bus" / "usb" / std::format("{:03}", *bus) / std::format("{:03}", *number);
        devices.push_back(UsbDeviceAccess{.bus = *bus,
                                          .device = *number,
                                          .product = readAttribute(entry / "product").value_or(""),
                                          .accessible = canReadAndWrite(node)});
    }
    std::ranges::sort(devices, [](const UsbDeviceAccess& a, const UsbDeviceAccess& b) {
        return a.bus != b.bus ? a.bus < b.bus : a.device < b.device;
    });
#endif
    return devices;
}

std::optional<Error> usbAccessDeniedError(std::span<const UsbDeviceAccess> devices) {
    std::string detail;
    for (const UsbDeviceAccess& device : devices) {
        if (device.accessible) {
            continue;
        }
        if (!detail.empty()) {
            detail += ", ";
        }
        detail += std::format("/dev/bus/usb/{:03}/{:03} not accessible", device.bus, device.device);
    }
    if (detail.empty()) {
        return std::nullopt;
    }
    return makeError(ErrorCode::UsbAccessDenied, detail);
}

} // namespace hio::diag
