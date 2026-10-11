#pragma once

// Input of `hio chester lte flash` and the GUI's LTE flashing (ADR-0040, ADR-0062): the file is
// classified by its content, not by its extension.
//
//   zip with exactly the top-level entries "modem.zip" and "application.hex" (case-sensitive,
//       as py-hardwario)                                     -> LteBundle
//   zip with Nordic modem firmware markers                   -> ModemOnly
//   Intel HEX                                                -> AppHex
//   anything else                                            -> firmware.invalid_image
//
// Modem firmware marker (to confirm in Phase 0 with real mfw_nrf91* files): a top-level entry
// named "firmware.update.image.segments.<N>.hex".

#include "hio/core/result.h"
#include "hio/firmware/firmware_image.h"

#include <QTemporaryDir>

#include <filesystem>
#include <memory>
#include <string_view>
#include <variant>

namespace hio::firmware {

struct LteBundle {
    std::filesystem::path modemZip; // extracted into extractDir
    FirmwareImage application;
    // Owns the extracted modem.zip; lives until the last user (the operation) is done.
    std::shared_ptr<QTemporaryDir> extractDir;
};

struct ModemOnly {
    std::filesystem::path path;
};

struct AppHex {
    FirmwareImage image;
};

using LteInput = std::variant<LteBundle, ModemOnly, AppHex>;

// Classifies and loads `path`. Bundles are extracted into a new QTemporaryDir (under
// QDir::tempPath()). Errors: firmware.invalid_image, firmware.hex_parse_error, io.*.
[[nodiscard]] Result<LteInput> classifyLteInput(const std::filesystem::path& path);

// True for a top-level entry name that marks Nordic modem firmware (see above).
[[nodiscard]] bool isModemFirmwareMarker(std::string_view entryName) noexcept;

} // namespace hio::firmware
