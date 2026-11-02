#pragma once

// Firmware identity -> console file names (plan/features/0028 F1, ADR-0200, ADR-0201).
//
// identityFromInfo(): the CHESTER `info show` answer (firmware name and version required; the
// strings are cut to the FirmwareIdentity bounds at a UTF-8 character boundary).
// identityFromBanner() (D10): a Zephyr boot banner "*** Booting <name> v<version> ***" only when it
// names the application, never an SDK banner (<name> one of "Zephyr OS", "Zephyr OS build",
// "nRF Connect SDK", "MCUboot", case-insensitive); "*** Using Zephyr OS ... ***" is ignored.
//
// consoleFileBase() (D1): "<profile>-<target>" plus, with an identity, "-<slug>-<version>", where
// slug = fileSlug(name) without a leading "<profile>-" ("CHESTER Clime" -> "clime") and version =
// versionSlug(version). Shared by the console file names and Save view (ADR-0201). Without an
// identity, or when a slug is empty, the base has no identity part.
//
// fileSlug(): ASCII letters lower-cased, digits kept, every other run of bytes (spaces,
// punctuation, non-ASCII) becomes one '-', trimmed of '-', cut to maxBytes. versionSlug():
// [0-9A-Za-z+_-] kept, '.' kept only when followed by a digit (so ".part" never appears), other
// runs become '-', trimmed of '-' (and of a '.' at the end), cut to maxBytes.

#include "hwio/core/firmware_identity.h"
#include "hwio/device/device_info.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace hwio::device {

using FirmwareIdentity = hwio::FirmwareIdentity;

inline constexpr std::size_t maxFileSlugBytes = 40;
inline constexpr std::size_t maxVersionSlugBytes = 32;

[[nodiscard]] std::optional<FirmwareIdentity> identityFromInfo(const DeviceInfo& info);
[[nodiscard]] std::optional<FirmwareIdentity> identityFromBanner(std::string_view line);

// Review round 1 (R1): the same firmware (name and version); another device (both device serials
// known and different; an empty serial is never a change).
[[nodiscard]] bool sameFirmware(const FirmwareIdentity& a, const FirmwareIdentity& b) noexcept;
[[nodiscard]] bool otherDevice(const FirmwareIdentity& a, const FirmwareIdentity& b) noexcept;

// "CHESTER Clime v3.0.0" (the marker line text).
[[nodiscard]] std::string identityText(const FirmwareIdentity& identity);

[[nodiscard]] std::string consoleFileBase(std::string_view profileId, std::string_view targetId,
                                          const std::optional<FirmwareIdentity>& identity);
[[nodiscard]] std::string fileSlug(std::string_view text, std::size_t maxBytes = maxFileSlugBytes);
[[nodiscard]] std::string versionSlug(std::string_view text,
                                      std::size_t maxBytes = maxVersionSlugBytes);

} // namespace hwio::device
