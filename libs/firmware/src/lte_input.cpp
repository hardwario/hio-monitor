#include "hio/firmware/lte_input.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/firmware/intel_hex.h"
#include "hio/firmware/zip_reader.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QIODevice>
#include <QLoggingCategory>
#include <QString>
#include <QTemporaryDir>
#include <QtLogging>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace hio::firmware {

namespace {

namespace fs = std::filesystem;

constexpr std::string_view bundleModem = "modem.zip";
constexpr std::string_view bundleApplication = "application.hex";

Error notSupported(const fs::path& path, std::string reason) {
    Error error =
        makeError(ErrorCode::FirmwareInvalidImage, toUtf8(path) + ": " + std::move(reason));
    error.message =
        QCoreApplication::translate(
            "hio::firmware", "The file is not a CHESTER LTE bundle, modem firmware or HEX file.")
            .toStdString();
    return error;
}

enum class FileKind : std::uint8_t { Zip, Hex, Other };

Result<FileKind> sniff(const fs::path& path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        return std::unexpected(makeError(ErrorCode::IoFileNotFound, toUtf8(path)));
    }
    QFile file(toQString(toUtf8(path)));
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(makeError(ErrorCode::IoReadFailed,
                                         toUtf8(path) + ": " + file.errorString().toStdString()));
    }
    const QByteArray head = file.read(4);
    if (head.startsWith("PK\x03\x04") || head.startsWith("PK\x05\x06")) {
        return FileKind::Zip;
    }
    // A HEX file starts with ':' (an UTF-8 BOM is not accepted, like any other prefix).
    if (head.startsWith(':')) {
        return FileKind::Hex;
    }
    return FileKind::Other;
}

bool topLevel(std::string_view name) { return name.find('/') == std::string_view::npos; }

} // namespace

bool isModemFirmwareMarker(std::string_view entryName) noexcept {
    constexpr std::string_view prefix = "firmware.update.image.segments.";
    constexpr std::string_view suffix = ".hex";
    if (!entryName.starts_with(prefix) || !entryName.ends_with(suffix) ||
        entryName.size() <= prefix.size() + suffix.size()) {
        return false;
    }
    const std::string_view number =
        entryName.substr(prefix.size(), entryName.size() - prefix.size() - suffix.size());
    return std::ranges::all_of(number, [](char c) { return c >= '0' && c <= '9'; });
}

Result<LteInput> classifyLteInput(const fs::path& path, const std::stop_token& stop) {
    if (stop.stop_requested()) {
        return std::unexpected(
            makeError(ErrorCode::Cancelled, toUtf8(path) + ": classification stopped"));
    }
    auto kind = sniff(path);
    if (!kind) {
        return std::unexpected(std::move(kind).error());
    }
    if (*kind == FileKind::Hex) {
        auto image = readIntelHexFile(path, stop);
        if (!image) {
            return std::unexpected(std::move(image).error());
        }
        qCInfo(log::lcFirmware).noquote() << "LTE input: HEX file" << toQString(toUtf8(path));
        return AppHex{.image = std::move(*image)};
    }
    if (*kind != FileKind::Zip) {
        return std::unexpected(notSupported(path, "neither a zip archive nor a HEX file"));
    }
    auto zip = ZipReader::open(path);
    if (!zip) {
        Error error = std::move(zip).error();
        if (error.code == ErrorCode::FirmwareInvalidImage) {
            return std::unexpected(notSupported(path, error.detail));
        }
        return std::unexpected(std::move(error));
    }
    const auto& entries = zip->entries();
    const auto has = [&entries](std::string_view name) {
        return std::ranges::any_of(entries, [name](const ZipEntry& e) { return e.name == name; });
    };
    if (entries.size() == 2 && has(bundleModem) && has(bundleApplication)) {
        auto hexText = zip->extract(bundleApplication, maxHexFileBytes, stop);
        if (!hexText) {
            return std::unexpected(std::move(hexText).error());
        }
        auto application =
            parseIntelHex(*hexText, toUtf8(path.filename()) + "/application.hex", stop);
        if (!application) {
            return std::unexpected(std::move(application).error());
        }
        auto directory = std::make_shared<QTemporaryDir>();
        if (!directory->isValid()) {
            return std::unexpected(
                makeError(ErrorCode::IoWriteFailed,
                          "temporary directory: " + directory->errorString().toStdString()));
        }
        auto modem = zip->extractTo(bundleModem, fs::path(directory->path().toStdU16String()),
                                    ZipLimits{}.maxEntryBytes, stop);
        if (!modem) {
            return std::unexpected(std::move(modem).error());
        }
        qCInfo(log::lcFirmware).noquote()
            << "LTE input: bundle" << toQString(toUtf8(path)) << "- modem.zip extracted to"
            << toQString(toUtf8(*modem));
        return LteBundle{.modemZip = std::move(*modem),
                         .application = std::move(*application),
                         .extractDir = std::move(directory)};
    }
    if (std::ranges::any_of(entries, [](const ZipEntry& e) {
            return topLevel(e.name) && isModemFirmwareMarker(e.name);
        })) {
        qCInfo(log::lcFirmware).noquote() << "LTE input: modem firmware" << toQString(toUtf8(path));
        return ModemOnly{.path = path};
    }
    std::string names;
    for (const ZipEntry& entry : entries) {
        names += names.empty() ? entry.name : ", " + entry.name;
    }
    return std::unexpected(
        notSupported(path, std::format("zip with {} entries ({}) is neither a bundle (modem.zip + "
                                       "application.hex) nor modem firmware",
                                       entries.size(), names)));
}

} // namespace hio::firmware
