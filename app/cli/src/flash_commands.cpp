#include "flash_commands.h"

#include "cli_app.h"
#include "cli_log.h"
#include "confirm.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/core/settings.h"
#include "hio/device/device_session.h"
#include "hio/device/operation.h"
#include "hio/device/pib.h"
#include "hio/device/profile.h"
#include "hio/device/session_registry.h"
#include "hio/firmware/firmware_cache.h"
#include "hio/firmware/firmware_catalog.h"
#include "hio/firmware/firmware_image.h"
#include "hio/firmware/intel_hex.h"
#include "hio/firmware/lte_input.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/nrfutil/nrfutil.h"
#include "jlink_commands.h"
#include "output.h"
#include "probe_options.h"
#include "progress_ui.h"
#include "terminal.h"

#include <QByteArrayView>
#include <QFile>
#include <QIODevice>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QString>
#include <QtTypes>

#include <CLI/CLI.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace hio::cli {

namespace {

namespace fs = std::filesystem;
using device::OperationId;
using device::OperationResult;

constexpr std::string_view profileId = "chester";
constexpr std::string_view completed = "Successfully completed";

const device::DeviceProfile& chester() { return *device::ProfileRegistry::find(profileId); }

const device::TargetProfile& targetOf(std::string_view id) {
    return *device::findTarget(chester(), id);
}

fs::path toPath(const std::string& text) { return {toQString(text).toStdU16String()}; }

Error usage(const QString& message, std::string detail) {
    Error error = makeError(ErrorCode::UsageInvalidArgument, std::move(detail));
    error.message = message.toStdString();
    return error;
}

std::string kib(std::uint64_t bytes) { return std::format("{} KiB", (bytes + 1023) / 1024); }

// ---- the session and one operation ----------------------------------------------------------

struct OperationOutcome {
    OperationResult result;
    std::uint32_t probeSerial = 0;
};

using StartFn = std::function<Result<OperationId>(device::DeviceSession&)>;

// Opens a flash-only session (attachRtt = false) on `targetId`, starts the operation and waits for
// its result with progress. Ctrl+C cancels the operation (exit 130); the session is closed (the
// probe released) before returning.
Result<OperationOutcome> runOperation(const Context& context, const GlobalOptions& globals,
                                      const ProbeOptions& group, const ProbeOptions& own,
                                      std::string_view targetId, ProgressUi& ui,
                                      const StartFn& start,
                                      std::optional<fs::path> nrfUtil = std::nullopt) {
    auto location = locateJLinkLibrary(globals);
    if (!location) {
        return std::unexpected(std::move(location).error());
    }
    const device::TargetProfile& target = targetOf(targetId);
    device::SessionOptions options;
    options.profile = &chester();
    options.targetId = targetId;
    options.probeSn = serialNumberOf(group, own);
    auto speed = speedOf(group, own, target.defaultSpeedKhz);
    if (!speed) {
        return std::unexpected(std::move(speed).error());
    }
    options.speedKhz = *speed;
    options.latencyMs = target.defaultLatencyMs;
    options.autoReconnect = false;
    options.attachRtt = false;
    if (nrfUtil) {
        options.nrfUtil = nrfutil::defaultLocatorOptions(std::move(nrfUtil), nullptr);
    }
    device::SessionRegistry registry;
    auto session = device::DeviceSession::open(registry, *location, std::move(options));
    if (!session) {
        return std::unexpected(std::move(session).error());
    }
    device::DeviceSession& device = **session;
    auto id = start(device);
    if (!id) {
        device.close();
        return std::unexpected(std::move(id).error());
    }
    bool announced = false;
    const auto announce = [&] {
        if (!announced && device.probe().serialNumber != 0) {
            announced = true;
            const jlink::ProbeInfo probe = device.probe();
            qCInfo(lcCli) << "Using J-Link" << probe.serialNumber;
            ui.info(probe.product.empty()
                        ? std::format("Using J-Link {}", probe.serialNumber)
                        : std::format("Using J-Link {} ({})", probe.serialNumber, probe.product));
        }
    };
    OperationResult result = device::waitForOperation(
        device, *id,
        [&](const device::OperationProgress& progress) {
            announce();
            ui.progress(progress);
        },
        context.stop);
    announce();
    ui.finish();
    const std::uint32_t serial = device.probe().serialNumber;
    device.close(); // releases the probe (and its lock) now; joins the worker's current step
    // Review R13: when the wait gave up after Ctrl+C, the worker may still have finished the
    // operation (a recover, an erase-all, or a real error such as a changed PIB): report that.
    if (auto real = device.finishedResult(*id)) {
        result = std::move(*real);
    }
    if (!result.status) {
        return std::unexpected(result.status.error());
    }
    if (result.chip && !result.chip->name.empty()) {
        ui.info(std::format("Target: {} ({})", result.chip->name, targetId));
    }
    return OperationOutcome{.result = std::move(result), .probeSerial = serial};
}

QJsonObject resultJson(std::string_view targetId, const OperationResult& result) {
    QJsonObject data;
    data.insert(QStringLiteral("type"), QStringLiteral("result"));
    data.insert(QStringLiteral("target"), toQString(targetId));
    data.insert(QStringLiteral("operation"), toQString(device::operationKindId(result.kind)));
    data.insert(QStringLiteral("part"), result.chip && !result.chip->name.empty()
                                            ? QJsonValue(toQString(result.chip->name))
                                            : QJsonValue(QJsonValue::Null));
    data.insert(QStringLiteral("durationMs"), static_cast<double>(result.duration.count()));
    return data;
}

QJsonObject imageJson(const firmware::FirmwareImage& image, std::string_view source,
                      const QString& key, const QString& value) {
    QJsonObject json;
    json.insert(QStringLiteral("source"), toQString(source));
    json.insert(key, value);
    json.insert(QStringLiteral("bytes"), static_cast<double>(image.totalBytes()));
    json.insert(QStringLiteral("sha256"), toQString(image.sha256()));
    return json;
}

// ---- app flash -------------------------------------------------------------------------------

struct AppFlashOptions {
    ProbeOptions probe;
    std::string input;
    bool halt = false;
    bool allowUicr = false;
    bool noCache = false;
};

struct LoadedImage {
    std::shared_ptr<const firmware::FirmwareImage> image;
    QJsonObject json;
};

Result<LoadedImage> loadAppImage(const Context& context, const AppFlashOptions& options,
                                 ProgressUi& ui) {
    auto input = resolveFlashInput(options.input, fs::current_path());
    if (!input) {
        return std::unexpected(std::move(input).error());
    }
    if (const auto* file = std::get_if<FlashFile>(&*input)) {
        auto image = firmware::readIntelHexFile(file->path);
        if (!image) {
            return std::unexpected(std::move(image).error());
        }
        ui.info(std::format("Firmware {} ({})", toUtf8(file->path), kib(image->totalBytes())));
        QJsonObject json =
            imageJson(*image, "file", QStringLiteral("path"), toQString(toUtf8(file->path)));
        return LoadedImage{.image =
                               std::make_shared<const firmware::FirmwareImage>(std::move(*image)),
                           .json = std::move(json)};
    }
    const auto& id = std::get<FlashCatalogId>(*input);
    auto firmwareId = firmware::FirmwareId::parse(id.id);
    if (!firmwareId) {
        return std::unexpected(std::move(firmwareId).error());
    }
    firmware::FirmwareCache cache(firmware::FirmwareCache::defaultDirectory());
    firmware::FirmwareCatalog catalog(
        firmware::CatalogOptions{.baseUrl = firmware::catalogBaseUrl(chester().firmwareCatalogUrl),
                                 .transferTimeout = std::chrono::seconds(60),
                                 .maxBodyBytes = 32ULL * 1024 * 1024},
        &cache);
    auto operation = catalog.download(
        *firmwareId, firmware::DownloadOptions{.noCache = options.noCache},
        [&ui](std::uint64_t done, std::uint64_t total) { ui.progress("downloading", done, total); },
        context.stop);
    auto downloaded = firmware::waitForDownload(*operation);
    ui.finish();
    if (!downloaded) {
        return std::unexpected(std::move(downloaded).error());
    }
    for (const Error& warning : downloaded->warnings) {
        ui.warning(warning);
    }
    ui.info(std::format("Firmware {}: {} ({})", firmwareId->value(),
                        downloaded->fromCache ? "cached" : "downloaded",
                        kib(downloaded->image->totalBytes())));
    QJsonObject json = imageJson(*downloaded->image, "catalog", QStringLiteral("id"),
                                 toQString(firmwareId->value()));
    json.insert(QStringLiteral("cached"), downloaded->fromCache);
    return LoadedImage{.image = downloaded->image, .json = std::move(json)};
}

Result<CommandResult> appFlash(const Context& context, const GlobalOptions& globals,
                               const ProbeOptions& group, const AppFlashOptions& options) {
    ProgressUi ui(context, std::cerr, stderrIsTerminal());
    auto loaded = loadAppImage(context, options, ui);
    if (!loaded) {
        return std::unexpected(std::move(loaded).error());
    }
    const device::FlashOptions flash{.halt = options.halt, .allowUicr = options.allowUicr};
    auto outcome = runOperation(
        context, globals, group, options.probe, "app", ui,
        [&](device::DeviceSession& session) { return session.flash("app", loaded->image, flash); });
    if (!outcome) {
        return std::unexpected(std::move(outcome).error());
    }
    QJsonObject data = resultJson("app", outcome->result);
    data.insert(QStringLiteral("image"), loaded->json);
    data.insert(QStringLiteral("halted"), options.halt);
    return CommandResult{.data = data, .humanText = std::string(completed), .streamed = false};
}

// ---- erase / reset / recover ---------------------------------------------------------------

Result<CommandResult> erase(const Context& context, const GlobalOptions& globals,
                            const ProbeOptions& group, const ProbeOptions& own,
                            std::string_view targetId, device::EraseScope scope, bool yes) {
    if (scope == device::EraseScope::All) {
        const std::string question = std::format(
            "This erases all flash, UICR and the Product Information Block of the {} MCU.\n"
            "Continue? [y/N] ",
            targetOf(targetId).displayName); // the CLI is English only (ADR-0027)
        if (Status confirmed = confirmDestructive(context, yes, question); !confirmed) {
            return std::unexpected(std::move(confirmed).error());
        }
    }
    ProgressUi ui(context, std::cerr, stderrIsTerminal());
    auto outcome = runOperation(
        context, globals, group, own, targetId, ui,
        [&](device::DeviceSession& session) { return session.erase(targetId, scope); });
    if (!outcome) {
        return std::unexpected(std::move(outcome).error());
    }
    QJsonObject data = resultJson(targetId, outcome->result);
    data.insert(QStringLiteral("scope"),
                scope == device::EraseScope::All ? QStringLiteral("all") : QStringLiteral("code"));
    return CommandResult{.data = data, .humanText = std::string(completed), .streamed = false};
}

Result<CommandResult> reset(const Context& context, const GlobalOptions& globals,
                            const ProbeOptions& group, const ProbeOptions& own,
                            std::string_view targetId, bool halt) {
    ProgressUi ui(context, std::cerr, stderrIsTerminal(), true);
    const device::ResetMode mode = halt ? device::ResetMode::Halt : device::ResetMode::Go;
    auto outcome =
        runOperation(context, globals, group, own, targetId, ui,
                     [&](device::DeviceSession& session) { return session.reset(targetId, mode); });
    if (!outcome) {
        return std::unexpected(std::move(outcome).error());
    }
    QJsonObject data = resultJson(targetId, outcome->result);
    data.insert(QStringLiteral("halted"), halt);
    return CommandResult{.data = data, .humanText = std::string(completed), .streamed = false};
}

Result<CommandResult> recover(const Context& context, const GlobalOptions& globals,
                              const ProbeOptions& group, const ProbeOptions& own,
                              std::string_view targetId, bool yes) {
    const std::string question = std::format(
        "This erases ALL flash, UICR and the Product Information Block of the {} MCU and removes "
        "read-back protection.\nContinue? [y/N] ",
        targetOf(targetId).displayName); // the CLI is English only (ADR-0027)
    if (Status confirmed = confirmDestructive(context, yes, question); !confirmed) {
        return std::unexpected(std::move(confirmed).error());
    }
    ProgressUi ui(context, std::cerr, stderrIsTerminal());
    ui.info("Recovering (can take up to 60 s)...");
    auto outcome =
        runOperation(context, globals, group, own, targetId, ui,
                     [&](device::DeviceSession& session) { return session.recover(targetId); });
    if (!outcome) {
        return std::unexpected(std::move(outcome).error());
    }
    return CommandResult{.data = resultJson(targetId, outcome->result),
                         .humanText = std::string(completed),
                         .streamed = false};
}

// ---- pib read / uicr read --------------------------------------------------------------------

Result<std::vector<std::byte>> readPibBytes(const Context& context, const GlobalOptions& globals,
                                            const ProbeOptions& group, const ProbeOptions& own) {
    ProgressUi ui(context, std::cerr, false, true);
    const device::TargetProfile& app = targetOf("app");
    const auto address = static_cast<std::uint32_t>(device::pibRange(app).start);
    auto outcome =
        runOperation(context, globals, group, own, "app", ui, [&](device::DeviceSession& session) {
            return session.readMemory("app", address, device::pibSize);
        });
    if (!outcome) {
        return std::unexpected(std::move(outcome).error());
    }
    return std::move(outcome->result.data);
}

Result<CommandResult> pibRead(const Context& context, const GlobalOptions& globals,
                              const ProbeOptions& group, const ProbeOptions& own) {
    auto bytes = readPibBytes(context, globals, group, own);
    if (!bytes) {
        return std::unexpected(std::move(bytes).error());
    }
    if (bytes->size() != device::pibSize) {
        return std::unexpected(makeError(ErrorCode::InternalUnexpected, "short PIB read"));
    }
    auto pib = device::decodePib(std::span<const std::byte, 128>(bytes->data(), 128));
    if (!pib) {
        return std::unexpected(std::move(pib).error());
    }
    // py-hardwario keys and formats.
    QJsonObject data;
    data.insert(QStringLiteral("signature"), toQString(std::format("0x{:08x}", pib->signature)));
    data.insert(QStringLiteral("version"), toQString(std::format("0x{:02x}", pib->version)));
    data.insert(QStringLiteral("size"), toQString(std::format("0x{:02x}", pib->size)));
    data.insert(QStringLiteral("crc"), toQString(std::format("0x{:08x}", pib->crc)));
    data.insert(QStringLiteral("vendor_name"), toQString(pib->vendorName));
    data.insert(QStringLiteral("product_name"), toQString(pib->productName));
    data.insert(QStringLiteral("hw_variant"), toQString(pib->hwVariant));
    data.insert(QStringLiteral("hw_revision"), toQString(pib->hwRevision));
    data.insert(QStringLiteral("serial_number"), toQString(pib->serialNumber));
    data.insert(QStringLiteral("claim_token"), toQString(pib->claimToken));
    data.insert(QStringLiteral("ble_passkey"), toQString(pib->blePasskey));
    std::string text;
    text += std::format("Vendor name: {}\n", pib->vendorName);
    text += std::format("Product name: {}\n", pib->productName);
    text += std::format("Hardware variant: {}\n", pib->hwVariant);
    text += std::format("Hardware revision: {}\n", pib->hwRevision);
    text += std::format("Serial number: {}\n", pib->serialNumber);
    text += std::format("Claim token: {}\n", pib->claimToken);
    text += std::format("BLE passkey: {}", pib->blePasskey);
    return CommandResult{.data = data, .humanText = text, .streamed = false};
}

std::string hexOf(std::span<const std::byte> bytes) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string text;
    for (const std::byte b : bytes) {
        const auto value = std::to_integer<unsigned>(b);
        text += digits[value >> 4U];
        text += digits[value & 0x0FU];
    }
    return text;
}

Result<CommandResult> uicrRead(const Context& context, const GlobalOptions& globals,
                               const ProbeOptions& group, const ProbeOptions& own,
                               const std::string& format, const std::string& file) {
    auto bytes = readPibBytes(context, globals, group, own);
    if (!bytes) {
        return std::unexpected(std::move(bytes).error());
    }
    const std::string hex = hexOf(*bytes);
    QJsonObject data;
    data.insert(QStringLiteral("format"), toQString(format));
    data.insert(QStringLiteral("file"), toQString(file));
    data.insert(QStringLiteral("bytes"), static_cast<double>(bytes->size()));
    data.insert(QStringLiteral("hex"), toQString(hex));
    const auto content =
        format == "hex" ? std::as_bytes(std::span(hex)) : std::span<const std::byte>(*bytes);
    if (file == "-") {
        if (context.mode == OutputMode::Human) {
            // py-hardwario compatible: the raw content on stdout, no newline.
            std::cout.write(
                QByteArrayView(content.data(), static_cast<qsizetype>(content.size())).data(),
                static_cast<std::streamsize>(content.size()));
            std::cout.flush();
        }
    } else {
        QFile out(toQString(file));
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
            out.write(QByteArrayView(content.data(), static_cast<qsizetype>(content.size())).data(),
                      static_cast<qint64>(content.size())) != static_cast<qint64>(content.size())) {
            return std::unexpected(
                makeError(ErrorCode::IoWriteFailed, file + ": " + out.errorString().toStdString()));
        }
    }
    return CommandResult{.data = data, .humanText = {}, .streamed = false};
}

// ---- lte flash -------------------------------------------------------------------------------

// The nRF Util of --nrfutil / HIO_NRFUTIL / the setting / discovery, checked before the device is
// touched (ADR-0042). Errors: dependency.*.
Result<fs::path> checkNrfUtil(const GlobalOptions& globals, ProgressUi& ui) {
    std::optional<fs::path> option;
    if (!globals.nrfutil.empty()) {
        option = toPath(globals.nrfutil);
    }
    auto settings = Settings::openDefault();
    const nrfutil::LocatorOptions locator =
        nrfutil::defaultLocatorOptions(std::move(option), settings ? &*settings : nullptr);
    auto executable = nrfutil::locate(locator);
    if (!executable) {
        return std::unexpected(std::move(executable).error());
    }
    auto info = nrfutil::checkVersions(*executable);
    if (!info) {
        return std::unexpected(std::move(info).error());
    }
    ui.info(std::format("Using nRF Util {} (device {})", nrfutil::formatVersion(info->core),
                        nrfutil::formatVersion(info->device)));
    return info->executable;
}

Result<CommandResult> lteFlash(const Context& context, const GlobalOptions& globals,
                               const ProbeOptions& group, const ProbeOptions& own,
                               const std::string& file, bool allowUicr) {
    const fs::path path = toPath(file);
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return std::unexpected(
            usage(QStringLiteral("The file %1 does not exist.").arg(toQString(file)), file));
    }
    auto input = firmware::classifyLteInput(path);
    if (!input) {
        return std::unexpected(std::move(input).error());
    }
    ProgressUi ui(context, std::cerr, stderrIsTerminal());
    const device::FlashOptions flash{.halt = false, .allowUicr = allowUicr};
    QJsonObject image;
    StartFn start;
    std::optional<fs::path> nrfUtil;
    if (auto* bundle = std::get_if<firmware::LteBundle>(&*input)) {
        ui.info(std::format("Bundle: modem firmware {} + application.hex ({})",
                            toUtf8(path.filename()), kib(bundle->application.totalBytes())));
        auto checked = checkNrfUtil(globals, ui);
        if (!checked) {
            return std::unexpected(std::move(checked).error());
        }
        nrfUtil = *checked;
        image = imageJson(bundle->application, "bundle", QStringLiteral("path"),
                          toQString(toUtf8(path)));
        auto shared = std::make_shared<const firmware::LteBundle>(std::move(*bundle));
        start = [shared, flash](device::DeviceSession& session) {
            return session.flashLteBundle("lte", shared, flash);
        };
    } else if (const auto* modem = std::get_if<firmware::ModemOnly>(&*input)) {
        ui.info(std::format("Modem firmware {}", toUtf8(modem->path.filename())));
        auto checked = checkNrfUtil(globals, ui);
        if (!checked) {
            return std::unexpected(std::move(checked).error());
        }
        nrfUtil = *checked;
        image.insert(QStringLiteral("source"), QStringLiteral("modem"));
        image.insert(QStringLiteral("path"), toQString(toUtf8(modem->path)));
        start = [zip = modem->path](device::DeviceSession& session) {
            return session.updateModem("lte", zip);
        };
    } else {
        auto& hex = std::get<firmware::AppHex>(*input);
        ui.info(std::format("Firmware {} ({})", toUtf8(path), kib(hex.image.totalBytes())));
        image = imageJson(hex.image, "file", QStringLiteral("path"), toQString(toUtf8(path)));
        auto shared = std::make_shared<const firmware::FirmwareImage>(std::move(hex.image));
        start = [shared, flash](device::DeviceSession& session) {
            return session.flash("lte", shared, flash);
        };
    }
    auto outcome = runOperation(context, globals, group, own, "lte", ui, start, nrfUtil);
    if (!outcome) {
        return std::unexpected(std::move(outcome).error());
    }
    QJsonObject data = resultJson("lte", outcome->result);
    data.insert(QStringLiteral("image"), image);
    data.insert(QStringLiteral("halted"), false);
    return CommandResult{.data = data, .humanText = std::string(completed), .streamed = false};
}

} // namespace

Result<FlashInput> resolveFlashInput(const std::string& argument, const fs::path& directory) {
    if (!argument.empty()) {
        const fs::path path = toPath(argument);
        std::error_code ec;
        if (fs::exists(path, ec)) {
            return FlashFile{.path = path};
        }
        if (firmware::FirmwareId::looksLikeId(argument)) {
            return FlashCatalogId{.id = argument};
        }
        return std::unexpected(usage(
            QStringLiteral("The path %1 does not exist (and it is not a 32-digit firmware ID).")
                .arg(toQString(argument)),
            argument));
    }
    static constexpr std::array<std::string_view, 3> names{"merged.hex", "zephyr.hex",
                                                           "merged_chester_nrf52840.hex"};
    std::string searched;
    for (const fs::path& base : {directory / "build", directory / "build" / "zephyr"}) {
        for (const std::string_view name : names) {
            const fs::path candidate = base / name;
            searched += searched.empty() ? toUtf8(candidate) : ", " + toUtf8(candidate);
            std::error_code ec;
            if (fs::is_regular_file(candidate, ec) && fs::file_size(candidate, ec) > 0 && !ec) {
                return FlashFile{.path = candidate};
            }
        }
    }
    return std::unexpected(
        usage(QStringLiteral("No HEX file was given and no build output was found."),
              "searched: " + searched));
}

void registerAppFlashCommands(CLI::App& appGroup, const std::shared_ptr<ProbeOptions>& group,
                              CommandSlot& slot, const GlobalOptions& globals) {

    auto flashOptions = std::make_shared<AppFlashOptions>();
    CLI::App* flash = appGroup.add_subcommand(
        "flash", "Flash application firmware from a HEX file or the firmware catalog (keeps the "
                 "UICR and the Product Information Block)");
    flash
        ->add_option("hex_file", flashOptions->input,
                     "HEX file, or the 32-digit firmware ID (default: the build output in "
                     "build/ or build/zephyr/)")
        ->option_text("HEX_FILE_OR_ID");
    flash->add_flag("--halt", flashOptions->halt, "Leave the CPU halted after flashing");
    flash->add_flag(
        "--allow-uicr", flashOptions->allowUicr,
        "Also program the image's UICR records (the Product Information Block is kept)");
    flash->add_flag("--no-cache", flashOptions->noCache,
                    "Download a catalog firmware even when it is cached");
    addProbeOptions(*flash, flashOptions->probe);
    flash->callback([&slot, &globals, group, flashOptions] {
        slot.command = [&globals, group, flashOptions](const Context& context) {
            return appFlash(context, globals, *group, *flashOptions);
        };
    });

    struct EraseOptions {
        ProbeOptions probe;
        bool all = false;
        bool yes = false;
    };
    auto eraseOptions = std::make_shared<EraseOptions>();
    CLI::App* eraseCommand = appGroup.add_subcommand(
        "erase", "Erase the application firmware (keeps the UICR); --all erases everything");
    eraseCommand->add_flag("--all", eraseOptions->all,
                           "Erase all flash and the UICR (the Product Information Block is lost)");
    eraseCommand->add_flag("--yes", eraseOptions->yes, "Do not ask for confirmation");
    addProbeOptions(*eraseCommand, eraseOptions->probe);
    eraseCommand->callback([&slot, &globals, group, eraseOptions] {
        slot.command = [&globals, group, eraseOptions](const Context& context) {
            return erase(context, globals, *group, eraseOptions->probe, "app",
                         eraseOptions->all ? device::EraseScope::All : device::EraseScope::Code,
                         eraseOptions->yes);
        };
    });

    struct ResetOptions {
        ProbeOptions probe;
        bool halt = false;
    };
    auto resetOptions = std::make_shared<ResetOptions>();
    CLI::App* resetCommand = appGroup.add_subcommand("reset", "Reset the application MCU");
    resetCommand->add_flag("--halt", resetOptions->halt, "Leave the CPU halted");
    addProbeOptions(*resetCommand, resetOptions->probe);
    resetCommand->callback([&slot, &globals, group, resetOptions] {
        slot.command = [&globals, group, resetOptions](const Context& context) {
            return reset(context, globals, *group, resetOptions->probe, "app", resetOptions->halt);
        };
    });

    struct RecoverOptions {
        ProbeOptions probe;
        bool yes = false;
    };
    auto recoverOptions = std::make_shared<RecoverOptions>();
    CLI::App* recoverCommand = appGroup.add_subcommand(
        "recover", "Recover a read-back protected application MCU (erases everything)");
    recoverCommand->add_flag("--yes", recoverOptions->yes, "Do not ask for confirmation");
    addProbeOptions(*recoverCommand, recoverOptions->probe);
    recoverCommand->callback([&slot, &globals, group, recoverOptions] {
        slot.command = [&globals, group, recoverOptions](const Context& context) {
            return recover(context, globals, *group, recoverOptions->probe, "app",
                           recoverOptions->yes);
        };
    });

    CLI::App* pib = appGroup.add_subcommand("pib", "HARDWARIO Product Information Block");
    pib->require_subcommand(1);
    auto pibOptions = std::make_shared<ProbeOptions>();
    addProbeOptions(*pib, *pibOptions);
    auto pibReadOptions = std::make_shared<ProbeOptions>();
    CLI::App* pibReadCommand =
        pib->add_subcommand("read", "Read the Product Information Block from the UICR");
    addProbeOptions(*pibReadCommand, *pibReadOptions);
    pibReadCommand->callback([&slot, &globals, group, pibOptions, pibReadOptions] {
        slot.command = [&globals, group, pibOptions, pibReadOptions](const Context& context) {
            const ProbeOptions own{.serialNumber = serialNumberOf(*pibOptions, *pibReadOptions),
                                   .speed = pibReadOptions->speed ? pibReadOptions->speed
                                                                  : pibOptions->speed};
            return pibRead(context, globals, *group, own);
        };
    });

    CLI::App* uicr = appGroup.add_subcommand("uicr", "UICR flash area");
    uicr->require_subcommand(1);
    auto uicrOptions = std::make_shared<ProbeOptions>();
    addProbeOptions(*uicr, *uicrOptions);
    struct UicrReadOptions {
        ProbeOptions probe;
        std::string format;
        std::string file;
    };
    auto uicrReadOptions = std::make_shared<UicrReadOptions>();
    CLI::App* uicrReadCommand = uicr->add_subcommand(
        "read", "Read the UICR customer area (128 bytes at UICR + 0x80) to FILE or stdout (-)");
    uicrReadCommand->add_option("--format", uicrReadOptions->format, "Output format")
        ->required()
        ->check(CLI::IsMember({"hex", "bin"}))
        ->option_text("{hex,bin}");
    uicrReadCommand->add_option("file", uicrReadOptions->file, "Output file, - for stdout")
        ->required()
        ->option_text("FILE");
    addProbeOptions(*uicrReadCommand, uicrReadOptions->probe);
    uicrReadCommand->callback([&slot, &globals, group, uicrOptions, uicrReadOptions] {
        slot.command = [&globals, group, uicrOptions, uicrReadOptions](const Context& context) {
            const ProbeOptions own{
                .serialNumber = serialNumberOf(*uicrOptions, uicrReadOptions->probe),
                .speed = uicrReadOptions->probe.speed ? uicrReadOptions->probe.speed
                                                      : uicrOptions->speed};
            return uicrRead(context, globals, *group, own, uicrReadOptions->format,
                            uicrReadOptions->file);
        };
    });
}

void registerLteCommands(CLI::App& chesterCommand, CommandSlot& slot,
                         const GlobalOptions& globals) {
    CLI::App* lte = chesterCommand.add_subcommand("lte", "LTE modem SoC commands");
    lte->require_subcommand(1);
    auto group = std::make_shared<ProbeOptions>();
    addProbeOptions(*lte, *group);

    struct FlashOptions {
        ProbeOptions probe;
        std::string file;
        bool allowUicr = false;
    };
    auto flashOptions = std::make_shared<FlashOptions>();
    CLI::App* flash = lte->add_subcommand(
        "flash", "Flash a CHESTER LTE bundle (modem firmware + application), a Nordic modem "
                 "firmware zip or an application HEX file");
    flash->add_option("file", flashOptions->file, "Bundle (.zip), modem firmware (.zip) or .hex")
        ->required()
        ->option_text("FILE");
    flash->add_flag(
        "--allow-uicr", flashOptions->allowUicr,
        "Also program the image's UICR records (the Product Information Block is kept)");
    addProbeOptions(*flash, flashOptions->probe);
    flash->callback([&slot, &globals, group, flashOptions] {
        slot.command = [&globals, group, flashOptions](const Context& context) {
            return lteFlash(context, globals, *group, flashOptions->probe, flashOptions->file,
                            flashOptions->allowUicr);
        };
    });

    struct YesOptions {
        ProbeOptions probe;
        bool yes = false;
    };
    auto eraseOptions = std::make_shared<YesOptions>();
    CLI::App* eraseCommand = lte->add_subcommand(
        "erase",
        "Erase all flash and the UICR of the LTE MCU (the Product Information Block is lost)");
    eraseCommand->add_flag("--yes", eraseOptions->yes, "Do not ask for confirmation");
    addProbeOptions(*eraseCommand, eraseOptions->probe);
    eraseCommand->callback([&slot, &globals, group, eraseOptions] {
        slot.command = [&globals, group, eraseOptions](const Context& context) {
            return erase(context, globals, *group, eraseOptions->probe, "lte",
                         device::EraseScope::All, eraseOptions->yes);
        };
    });

    auto resetOptions = std::make_shared<ProbeOptions>();
    CLI::App* resetCommand = lte->add_subcommand("reset", "Reset the LTE MCU and let it run");
    addProbeOptions(*resetCommand, *resetOptions);
    resetCommand->callback([&slot, &globals, group, resetOptions] {
        slot.command = [&globals, group, resetOptions](const Context& context) {
            return reset(context, globals, *group, *resetOptions, "lte", false);
        };
    });

    auto recoverOptions = std::make_shared<YesOptions>();
    CLI::App* recoverCommand = lte->add_subcommand(
        "recover", "Recover a read-back protected LTE MCU (erases everything, then programs the "
                   "debug-port unlock image)");
    recoverCommand->add_flag("--yes", recoverOptions->yes, "Do not ask for confirmation");
    addProbeOptions(*recoverCommand, recoverOptions->probe);
    recoverCommand->callback([&slot, &globals, group, recoverOptions] {
        slot.command = [&globals, group, recoverOptions](const Context& context) {
            return recover(context, globals, *group, recoverOptions->probe, "lte",
                           recoverOptions->yes);
        };
    });
}

} // namespace hio::cli
