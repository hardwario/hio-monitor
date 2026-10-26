#include "hio/device/profile.h"

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/firmware/firmware_image.h"
#include "profiles/profiles.h"

#include <QCoreApplication>
#include <QString>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace hio::device {

namespace {

// Registration: one line per device profile (ADR-0032).
constexpr std::array<const DeviceProfile*, 1> registeredProfiles{
    &profiles::chester,
};

} // namespace

const DeviceProfile* ProfileRegistry::find(std::string_view id) noexcept {
    // std::array's iterator is a pointer in libstdc++ and libc++ but a class in the MSVC STL
    // (`auto*` does not compile there).
    // NOLINTNEXTLINE(readability-qualified-auto)
    const auto it = std::ranges::find_if(
        registeredProfiles, [id](const DeviceProfile* profile) { return profile->id == id; });
    return it == registeredProfiles.end() ? nullptr : *it;
}

std::span<const DeviceProfile* const> ProfileRegistry::all() noexcept { return registeredProfiles; }

const DeviceProfile* ProfileRegistry::findByAdvertisement(
    std::string_view name, [[maybe_unused]] std::span<const std::string> serviceUuids) noexcept {
    // ADR-0142: by the name pattern only; an advertised NUS UUID is no profile match (Ruuvi tags
    // and other Nordic devices advertise it too).
    for (const DeviceProfile* profile : registeredProfiles) {
        if (!profile->ble) {
            continue;
        }
        const BleProfile& ble = *profile->ble;
        if (!ble.namePattern.empty() && name.find(ble.namePattern) != std::string_view::npos) {
            return profile;
        }
    }
    return nullptr;
}

const TargetProfile* findTarget(const DeviceProfile& profile, std::string_view id) noexcept {
    const auto it = std::ranges::find(profile.targets, id, &TargetProfile::id);
    return it == profile.targets.end() ? nullptr : &*it;
}

const ChipPart* findPart(const TargetProfile& target, std::uint32_t part) noexcept {
    const auto it = std::ranges::find(target.parts, part, &ChipPart::part);
    return it == target.parts.end() ? nullptr : &*it;
}

QString targetDisplayName(const TargetProfile& target) {
    const std::string source(target.displayName.empty() ? target.id : target.displayName);
    return QCoreApplication::translate("hio::device", source.c_str());
}

QString guideStepTitle(const ConnectionGuideStep& step) {
    return QCoreApplication::translate("hio::device", step.titleSourceText);
}

QString guideStepBody(const ConnectionGuideStep& step) {
    return QCoreApplication::translate("hio::device", step.bodySourceText);
}

QString powerHint(const DeviceProfile& profile) {
    if (profile.powerHintSourceText.empty()) {
        return {};
    }
    const std::string source(profile.powerHintSourceText);
    return QCoreApplication::translate("hio::device", source.c_str());
}

QString passkeyHint(const DeviceProfile& profile) {
    if (!profile.ble || std::string_view(profile.ble->passkeyHintSourceText).empty()) {
        return {};
    }
    return QCoreApplication::translate("hio::device", profile.ble->passkeyHintSourceText);
}

firmware::AddressRange pibRange(const TargetProfile& target) noexcept {
    const std::uint64_t start = target.uicr.start + target.pibOffset;
    return firmware::AddressRange{.start = start, .end = start + pibSize};
}

namespace {

// The end of the 32-bit address space (exclusive).
constexpr std::uint64_t addressSpaceEnd = std::uint64_t{1} << 32U;

std::string rangeText(const firmware::AddressRange& range) {
    return std::format("0x{:08X}-0x{:08X}", range.start, range.end);
}

// The start of the Cortex-M Private Peripheral Bus: denied ranges from here on are system or
// debug registers (ADR-0164), below it the flash controller (ADR-0160).
constexpr std::uint64_t privatePeripheralBus = 0xE0000000;

QString hexAddress(std::uint64_t address) {
    return QStringLiteral("0x%1").arg(address, 8, 16, QLatin1Char('0'));
}

} // namespace

Status checkMemoryRead(std::uint32_t address, std::size_t size) {
    const firmware::AddressRange requested{.start = address, .end = std::uint64_t{address} + size};
    if (requested.end <= addressSpaceEnd) {
        return {};
    }
    Error error = makeError(ErrorCode::TargetAddressOutOfRange,
                            std::format("requested {}; the address space ends at 0x{:X}",
                                        rangeText(requested), addressSpaceEnd));
    error.cause = QCoreApplication::translate("hio::device",
                                              "The range runs past the end of the address space.")
                      .toStdString();
    error.suggestion =
        QCoreApplication::translate("hio::device", "Use a shorter range or a lower address.")
            .toStdString();
    return std::unexpected(std::move(error));
}

Status checkMemoryWrite(const TargetProfile& target, std::uint32_t address, std::size_t size) {
    const firmware::AddressRange requested{.start = address, .end = std::uint64_t{address} + size};
    const auto denied =
        std::ranges::find_if(target.writeDenied, [&](const firmware::AddressRange& range) {
            return range.overlaps(requested);
        });
    if (denied != target.writeDenied.end()) {
        Error error = makeError(
            ErrorCode::TargetAddressOutOfRange,
            std::format("requested {}; denied {}", rangeText(requested), rangeText(*denied)));
        error.cause = denied->start >= privatePeripheralBus
                          ? QCoreApplication::translate(
                                "hio::device",
                                "The range overlaps system or debug registers of the CPU, through "
                                "which a write could make the device run arbitrary code.")
                                .toStdString()
                          : QCoreApplication::translate(
                                "hio::device",
                                "The range overlaps the flash controller registers, whose tasks "
                                "erase flash, UICR and the product information.")
                                .toStdString();
        return std::unexpected(std::move(error));
    }
    const auto inside = [&](const firmware::AddressRange& range) {
        return !range.empty() && range.contains(requested);
    };
    if (inside(target.ram) || std::ranges::any_of(target.peripherals, inside)) {
        return {};
    }
    std::string allowed = target.ram.empty() ? std::string() : rangeText(target.ram);
    for (const firmware::AddressRange& range : target.peripherals) {
        if (!range.empty()) {
            allowed += (allowed.empty() ? "" : ", ") + rangeText(range);
        }
    }
    return std::unexpected(
        makeError(ErrorCode::TargetAddressOutOfRange,
                  std::format("requested {}; allowed {}", rangeText(requested), allowed)));
}

Status checkFlashWrite(const TargetProfile& target, std::uint32_t address, std::size_t size,
                       bool allowUicr) {
    const firmware::AddressRange range{.start = address, .end = std::uint64_t{address} + size};
    const firmware::AddressRange pib = pibRange(target);
    if (range.overlaps(pib)) {
        Error error = makeError(ErrorCode::FirmwareOutOfRange,
                                std::format("segment 0x{:08X}-0x{:08X} overlaps the PIB "
                                            "0x{:08X}-0x{:08X}",
                                            range.start, range.end, pib.start, pib.end));
        error.message = QCoreApplication::translate(
                            "hio::device",
                            "The firmware writes into the Product Information Block (address %1).")
                            .arg(hexAddress(std::max(range.start, pib.start)))
                            .toStdString();
        return std::unexpected(std::move(error));
    }
    if (!target.flash.contains(range) && !target.uicr.contains(range)) {
        const std::uint64_t outside =
            target.flash.contains(range.start) ? target.flash.end : range.start;
        Error error = makeError(ErrorCode::FirmwareOutOfRange,
                                std::format("segment 0x{:08X}-0x{:08X}; flash 0x{:08X}-0x{:08X}, "
                                            "UICR 0x{:08X}-0x{:08X}",
                                            range.start, range.end, target.flash.start,
                                            target.flash.end, target.uicr.start, target.uicr.end));
        error.message = QCoreApplication::translate(
                            "hio::device", "The firmware does not fit the %1 MCU (address %2).")
                            .arg(targetDisplayName(target), hexAddress(outside))
                            .toStdString();
        error.suggestion =
            QCoreApplication::translate("hio::device", "Check that the firmware is for the %1 MCU.")
                .arg(targetDisplayName(target))
                .toStdString();
        return std::unexpected(std::move(error));
    }
    if (!allowUicr && range.overlaps(target.uicr)) {
        return std::unexpected(
            makeError(ErrorCode::FirmwareUicrInImage,
                      std::format("UICR 0x{:08X}-0x{:08X}", target.uicr.start, target.uicr.end)));
    }
    return {};
}

} // namespace hio::device
