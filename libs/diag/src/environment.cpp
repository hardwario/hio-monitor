// The environment checks (plan/features/0007 § Check list texts, ADR-0068, ADR-0072, ADR-0073).

#include "hio/diag/environment.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/settings.h"
#include "hio/device/probe_lock.h"
#include "hio/device/probe_tools.h"
#include "hio/device/session_registry.h"
#include "hio/diag/check.h"
#include "hio/diag/process_scan.h"
#include "hio/diag/usb_access.h"
#include "hio/hw/ble/ble_host.h"
#include "hio/hw/ble/types.h"
#include "hio/hw/jlink/jlink_library.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/hw/jlink/probe_enumerator.h"
#include "hio/hw/jlink/types.h"
#include "hio/hw/nrfutil/nrfutil.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QString>
#include <QStringList>
#include <QTemporaryFile>
#include <QtContainerFwd>
#include <QtSystemDetection>
#include <QtTranslation>
#include <QtTypes>

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace hio::diag {

namespace {

namespace fs = std::filesystem;
using std::chrono::milliseconds;

// Titles (context "hio::diag"; plan/features/0007 § Check list texts).
const char* const jlinkTitle = QT_TRANSLATE_NOOP("hio::diag", "J-Link Software");
const char* const nrfUtilTitle = QT_TRANSLATE_NOOP("hio::diag", "nRF Util");
const char* const usbTitle = QT_TRANSLATE_NOOP("hio::diag", "USB access to J-Link");
const char* const holdersTitle = QT_TRANSLATE_NOOP("hio::diag", "Other programs using J-Link");
const char* const probesTitle = QT_TRANSLATE_NOOP("hio::diag", "J-Link probes");
const char* const bluetoothTitle = QT_TRANSLATE_NOOP("hio::diag", "Bluetooth");

// The `bluetooth` check's wait for the BLE thread, within the check's 5 s timeout.
constexpr milliseconds bluetoothQueryTimeout{4500};
const char* const firmwareTitle = QT_TRANSLATE_NOOP("hio::diag", "J-Link firmware");
const char* const dirsTitle = QT_TRANSLATE_NOOP("hio::diag", "Settings and log folders");

// State shared by the checks of one environmentChecks() list (one thread at a time).
struct SharedState {
    std::optional<jlink::LibraryLocation> location;
    std::unique_ptr<jlink::ProbeEnumerator> enumerator; // unloaded after probe-firmware
    std::vector<jlink::ProbeInfo> probes;
};

std::string folderOf(const fs::path& library) {
    const fs::path parent = library.parent_path();
    return toUtf8(parent.empty() ? library : parent);
}

device::RegistryOptions registryOptions(const EnvironmentContext& context) {
    device::RegistryOptions options = device::RegistryOptions::defaults();
    if (!context.instanceRoot.empty()) {
        options.instanceRoot = context.instanceRoot;
    }
    if (!context.lockDirectory.empty()) {
        options.lockDirectory = context.lockDirectory;
    }
    return options;
}

// ---- jlink-software ----------------------------------------------------------------------------

CheckResult checkJLink(const Check& check, const EnvironmentContext& context, SharedState& state) {
    auto location = jlink::locateLibrary(context.jlink);
    if (!location) {
        return problemResult(check, CheckStatus::Failed, std::move(location).error());
    }
    const fs::path instanceRoot = context.instanceRoot.empty()
                                      ? jlink::JLinkLibrary::defaultInstanceRoot()
                                      : context.instanceRoot;
    // ADR-0030 / ADR-0093: an own library copy, kept for the `probes` check.
    auto enumerator = jlink::ProbeEnumerator::load(*location, instanceRoot);
    if (!enumerator) {
        return problemResult(check, CheckStatus::Failed, std::move(enumerator).error());
    }
    const std::uint32_t version = enumerator->libraryVersion();
    state.location = *location;
    state.enumerator = std::make_unique<jlink::ProbeEnumerator>(std::move(*enumerator));
    if (context.facts) {
        context.facts->jlinkLibrary = location->path;
        context.facts->jlinkVersion = version;
    }
    return okResult(check, std::format("{} · {}", jlink::formatLibraryVersion(version),
                                       folderOf(location->path)));
}

// ---- nrfutil -----------------------------------------------------------------------------------

CheckResult checkNrfUtil(const Check& check, const EnvironmentContext& context) {
    const auto warning = [&check](Error error) {
        std::string text =
            QCoreApplication::translate("hio::diag", "%1 — needed only for LTE modem firmware")
                .arg(QString::fromStdString(error.message))
                .toStdString();
        return problemResult(check, CheckStatus::Warning, std::move(error), std::move(text));
    };
    auto executable = nrfutil::locate(context.nrfutil);
    if (!executable) {
        Error error = std::move(executable).error();
        CheckResult result = warning(error);
        result.result = QCoreApplication::translate(
                            "hio::diag", "Not found — needed only for LTE modem firmware")
                            .toStdString();
        return result;
    }
    auto info = nrfutil::checkVersions(*executable, std::chrono::seconds(4));
    if (!info) {
        return warning(std::move(info).error());
    }
    if (context.facts) {
        context.facts->nrfUtil = info->executable;
        context.facts->nrfUtilVersion = nrfutil::formatVersion(info->core);
        context.facts->nrfUtilDeviceVersion = nrfutil::formatVersion(info->device);
    }
    return okResult(check, QCoreApplication::translate("hio::diag", "%1, device %2")
                               .arg(toQString(nrfutil::formatVersion(info->core)),
                                    toQString(nrfutil::formatVersion(info->device)))
                               .toStdString());
}

// ---- usb-access --------------------------------------------------------------------------------

CheckResult checkUsb(const Check& check, const EnvironmentContext& context) {
#ifndef Q_OS_LINUX
    (void)context;
    return skippedResult(
        check, QCoreApplication::translate("hio::diag", "Not needed on this system").toStdString());
#else
    auto devices = checkUsbAccess(context.usbSysfsRoot, context.usbDevRoot);
    if (!devices) {
        qCInfo(log::lcDiag).noquote()
            << "USB access not checked:" << toQString(devices.error().detail);
        return skippedResult(
            check, QCoreApplication::translate("hio::diag", "Could not be checked").toStdString());
    }
    if (devices->empty()) {
        return skippedResult(
            check, QCoreApplication::translate("hio::diag", "No J-Link connected").toStdString());
    }
    if (auto denied = usbAccessDeniedError(*devices)) {
        return problemResult(check, CheckStatus::Failed, std::move(*denied));
    }
    return okResult(check,
                    QCoreApplication::translate("hio::diag", "Access granted").toStdString());
#endif
}

// ---- probe-holders -----------------------------------------------------------------------------

CheckResult checkHolders(const Check& check, const EnvironmentContext& context,
                         const std::stop_token& stop) {
    const std::shared_ptr<IProcessSource> source =
        context.processes ? context.processes : platformProcessSource();
    auto holders = findProbeHolders(*source, stop);
    if (!holders) {
        if (holders.error().code == ErrorCode::IoReadFailed) {
            return skippedResult(
                check,
                QCoreApplication::translate("hio::diag", "Could not be checked").toStdString());
        }
        Error error = makeError(ErrorCode::DiagCheckTimeout, holders.error().detail);
        error.message =
            QCoreApplication::translate("hio::diag", "The check \"%1\" did not finish in time.")
                .arg(QString::fromStdString(checkTitle(check.titleSourceText)))
                .toStdString();
        return problemResult(check, CheckStatus::Warning, std::move(error),
                             QCoreApplication::translate("hio::diag", "Incomplete").toStdString());
    }
    if (holders->empty()) {
        return okResult(check,
                        QCoreApplication::translate("hio::diag", "None found").toStdString());
    }
    QStringList running;
    std::string detail;
    for (const ProcessMatch& holder : *holders) {
        running.push_back(QCoreApplication::translate("hio::diag", "%1 (PID %2) is running")
                              .arg(QString::fromStdString(holder.displayName))
                              .arg(holder.pid));
        detail +=
            std::format("{}{} pid {}", detail.empty() ? "" : "; ", holder.executable, holder.pid);
    }
    const ProcessMatch& first = holders->front();
    Error error = makeError(ErrorCode::ProbeHeldByProgram, detail);
    error.message = QCoreApplication::translate(
                        "hio::diag", "%1 (PID %2) is running and may be using the J-Link.")
                        .arg(QString::fromStdString(first.displayName))
                        .arg(first.pid)
                        .toStdString();
    error.suggestion = QCoreApplication::translate("hio::diag", "Close %1 and try again.")
                           .arg(QString::fromStdString(first.displayName))
                           .toStdString();
    return problemResult(check, CheckStatus::Warning, std::move(error),
                         running.join(QStringLiteral("; ")).toStdString());
}

// ---- probes ------------------------------------------------------------------------------------

std::string ownerText(const device::ProbeLockOwner& owner) {
    return owner.pid > 0 ? std::format("{} (PID {})", owner.app, owner.pid) : owner.app;
}

CheckResult checkProbes(const Check& check, const EnvironmentContext& context, SharedState& state) {
    if (!state.enumerator) {
        return skippedResult(check, QCoreApplication::translate(
                                        "hio::diag", "Skipped: an earlier check did not pass")
                                        .toStdString());
    }
    auto probes = state.enumerator->enumerate();
    if (!probes) {
        return problemResult(check, CheckStatus::Warning, std::move(probes).error());
    }
    state.probes = *probes;
    if (probes->empty()) {
        return problemResult(check, CheckStatus::Warning, makeError(ErrorCode::ProbeNotFound),
                             QCoreApplication::translate("hio::diag", "None found").toStdString());
    }
    const device::RegistryOptions options = registryOptions(context);
    QStringList serials;
    for (const jlink::ProbeInfo& probe : *probes) {
        serials.push_back(QString::number(probe.serialNumber));
        if (context.facts) {
            EnvironmentFacts::Probe fact{.info = probe, .inUseBy = {}, .firmware = std::nullopt};
            if (const auto owner =
                    device::ProbeLock::owner(probe.serialNumber, options.lockDirectory)) {
                fact.inUseBy = ownerText(*owner);
            }
            context.facts->probes.push_back(std::move(fact));
        }
    }
    return okResult(check, QCoreApplication::translate("hio::diag", "%1 found: %2")
                               .arg(probes->size())
                               .arg(serials.join(QStringLiteral(", ")))
                               .toStdString());
}

// ---- probe-firmware ----------------------------------------------------------------------------

// The firmware of a probe in the report facts.
void recordFirmware(const EnvironmentContext& context, std::uint32_t serial,
                    const device::ProbeDetails& details) {
    if (!context.facts) {
        return;
    }
    for (EnvironmentFacts::Probe& fact : context.facts->probes) {
        if (fact.info.serialNumber == serial) {
            fact.info.firmware = details.probe.firmware;
            fact.firmware = details.firmwareStatus;
        }
    }
}

// probe.firmware_outdated for an outdated or newer probe firmware.
Error outdatedError(std::uint32_t serial, const device::ProbeDetails& details) {
    Error error = makeError(ErrorCode::ProbeFirmwareOutdated,
                            std::format("J-Link {} firmware: {}; library: {}", serial,
                                        details.probe.firmware, details.libraryFirmware));
    if (details.firmwareStatus == jlink::FirmwareStatus::Newer) {
        error.message = QCoreApplication::translate(
                            "hio::device", "The J-Link firmware is newer than the J-Link library.")
                            .toStdString();
    }
    return error;
}

CheckResult checkFirmware(const Check& check, const EnvironmentContext& context, SharedState& state,
                          const std::stop_token& stop) {
    state.enumerator.reset(); // the probes are listed: unload that copy (ADR-0030)
    if (!state.location) {
        return skippedResult(check, QCoreApplication::translate(
                                        "hio::diag", "Skipped: an earlier check did not pass")
                                        .toStdString());
    }
    std::vector<jlink::ProbeInfo> usbProbes;
    for (const jlink::ProbeInfo& probe : state.probes) {
        if (probe.connection == jlink::Connection::Usb) {
            usbProbes.push_back(probe);
        }
    }
    if (usbProbes.empty()) {
        return skippedResult(
            check, QCoreApplication::translate("hio::diag", "No J-Link connected").toStdString());
    }
    const device::RegistryOptions options = registryOptions(context);
    const device::SessionRegistry registry(options);
    std::optional<Error> outdated;
    std::optional<Error> notInspected; // the first probe that could not be opened (review V8)
    int checked = 0;
    int unknown = 0;
    int inUse = 0;
    for (const jlink::ProbeInfo& probe : usbProbes) {
        if (stop.stop_requested()) {
            break;
        }
        if (device::ProbeLock::owner(probe.serialNumber, options.lockDirectory)) {
            ++inUse; // a session holds it: not opened here (ADR-0058)
            continue;
        }
        auto details = device::inspectProbe(registry, *state.location, probe.serialNumber);
        if (!details) {
            Error error = std::move(details).error();
            qCInfo(log::lcDiag).noquote() << "Probe" << probe.serialNumber
                                          << "firmware not checked:" << toQString(error.detail);
            if (error.code == ErrorCode::ProbeInUseByOtherProcess) {
                ++inUse; // locked by a session since the owner() check above
            } else if (!notInspected) {
                error.detail = std::format("J-Link {}: {}", probe.serialNumber, error.detail);
                notInspected = std::move(error);
            }
            continue;
        }
        ++checked;
        recordFirmware(context, probe.serialNumber, *details);
        if (details->firmwareStatus == jlink::FirmwareStatus::Unknown) {
            ++unknown; // not a failure: nothing is known to be wrong (review R11, HIL 2026-10-16)
        } else if (details->firmwareStatus != jlink::FirmwareStatus::Current && !outdated) {
            outdated = outdatedError(probe.serialNumber, *details);
        }
    }
    if (outdated) {
        return problemResult(check, CheckStatus::Warning, std::move(*outdated));
    }
    if (notInspected) {
        // Not "in use": nothing holds the probe, it could not be opened (e.g. USB access, a
        // program outside HARDWARIO Monitor, a failing probe). The error says why.
        const QString text = QCoreApplication::translate("hio::diag", "Could not be checked: %1")
                                 .arg(toQString(notInspected->message));
        return problemResult(check, CheckStatus::Warning, std::move(*notInspected),
                             text.toStdString());
    }
    if (checked == 0) {
        return skippedResult(
            check, QCoreApplication::translate("hio::diag", "Not checked: the J-Link is in use")
                       .toStdString());
    }
    if (unknown > 0) {
        return okResult(check, QCoreApplication::translate("hio::diag", "Not known").toStdString());
    }
    if (inUse > 0) {
        return okResult(check, QCoreApplication::translate(
                                   "hio::diag", "Current (a J-Link in use was not checked)")
                                   .toStdString());
    }
    return okResult(check, QCoreApplication::translate("hio::diag", "Current").toStdString());
}

// ---- app-dirs ----------------------------------------------------------------------------------

std::optional<Error> checkWritable(const fs::path& directory) {
    if (directory.empty()) {
        return std::nullopt;
    }
    std::error_code error;
    fs::create_directories(directory, error);
    if (error) {
        return makeError(ErrorCode::IoWriteFailed,
                         std::format("{}: {}", toUtf8(directory), error.message()));
    }
    QTemporaryFile probe(QString::fromStdU16String(directory.u16string()) +
                         QStringLiteral("/.hio-write-check-XXXXXX"));
    if (!probe.open() || probe.write("x", 1) != 1) {
        return makeError(ErrorCode::IoWriteFailed, std::format("{}: {}", toUtf8(directory),
                                                               probe.errorString().toStdString()));
    }
    return std::nullopt;
}

CheckResult checkDirs(const Check& check, const EnvironmentContext& context) {
    for (const fs::path& directory : {context.settingsDir, context.logDir}) {
        if (auto error = checkWritable(directory)) {
            return problemResult(check, CheckStatus::Failed, std::move(*error));
        }
    }
    return okResult(check, QCoreApplication::translate("hio::diag", "Writable").toStdString());
}

// ---- bluetooth (M5, ADR-0088)
// ---------------------------------------------------------------------

CheckResult checkBluetooth(const Check& check, const EnvironmentContext& context) {
    std::shared_ptr<ble::BleHost> host = context.ble;
    if (!host) {
        host = std::make_shared<ble::BleHost>(); // short-lived: stopped when the check returns
    }
    auto adapter = host->queryAdapter(bluetoothQueryTimeout);
    if (!adapter) {
        Error error = adapter.error().code == ErrorCode::Timeout
                          ? makeError(ErrorCode::DiagCheckTimeout, adapter.error().detail)
                          : std::move(adapter).error();
        return problemResult(check, CheckStatus::Warning, std::move(error));
    }
    if (adapter->state == ble::AdapterState::PermissionUndetermined) {
        return okResult(
            check, QCoreApplication::translate("hio::diag", "Permission will be asked on first use")
                       .toStdString());
    }
    if (auto problem = ble::adapterError(*adapter)) {
        // Never Failed: Bluetooth is optional (ADR-0088).
        return problemResult(check, CheckStatus::Warning, std::move(*problem));
    }
    QString result = QCoreApplication::translate("hio::diag", "Adapter ready");
    if (!adapter->name.empty() && !adapter->address.empty()) {
        result +=
            QStringLiteral(" (%1, %2)").arg(toQString(adapter->name), toQString(adapter->address));
    } else if (!adapter->name.empty() || !adapter->address.empty()) {
        result += QStringLiteral(" (%1)").arg(
            toQString(adapter->name.empty() ? adapter->address : adapter->name));
    }
    return okResult(check, result.toStdString());
}

} // namespace

QJsonObject toJson(const EnvironmentFacts& facts) {
    QJsonObject tools;
    if (facts.jlinkLibrary) {
        QJsonObject jlink;
        jlink.insert(QStringLiteral("path"), toQString(toUtf8(*facts.jlinkLibrary)));
        jlink.insert(QStringLiteral("version"),
                     toQString(jlink::formatLibraryVersion(facts.jlinkVersion)));
        tools.insert(QStringLiteral("jlink"), jlink);
    }
    if (facts.nrfUtil) {
        QJsonObject nrfUtil;
        nrfUtil.insert(QStringLiteral("path"), toQString(toUtf8(*facts.nrfUtil)));
        nrfUtil.insert(QStringLiteral("version"), toQString(facts.nrfUtilVersion));
        nrfUtil.insert(QStringLiteral("deviceVersion"), toQString(facts.nrfUtilDeviceVersion));
        tools.insert(QStringLiteral("nrfutil"), nrfUtil);
    }
    QJsonArray probes;
    for (const EnvironmentFacts::Probe& probe : facts.probes) {
        QJsonObject json;
        json.insert(QStringLiteral("serialNumber"), static_cast<qint64>(probe.info.serialNumber));
        json.insert(QStringLiteral("product"), toQString(probe.info.product));
        json.insert(QStringLiteral("connection"), probe.info.connection == jlink::Connection::Usb
                                                      ? QStringLiteral("usb")
                                                      : QStringLiteral("ip"));
        json.insert(QStringLiteral("firmware"), toQString(probe.info.firmware));
        json.insert(QStringLiteral("firmwareStatus"),
                    probe.firmware ? toQString(jlink::firmwareStatusId(*probe.firmware))
                                   : QStringLiteral("not checked"));
        json.insert(QStringLiteral("inUseBy"), toQString(probe.inUseBy));
        probes.append(json);
    }
    tools.insert(QStringLiteral("probes"), probes);
    return tools;
}

EnvironmentContext defaultEnvironment(std::optional<fs::path> jlinkOption,
                                      std::optional<fs::path> nrfUtilOption) {
    EnvironmentContext context;
    auto settings = Settings::openDefault();
    const Settings* settingsPtr = settings ? &*settings : nullptr;
    context.jlink = jlink::defaultLocatorOptions(std::move(jlinkOption), settingsPtr);
    context.nrfutil = nrfutil::defaultLocatorOptions(std::move(nrfUtilOption), settingsPtr);
    context.settingsDir = Settings::defaultFilePath().parent_path();
    context.logDir = log::defaultLogDirectory();
    return context;
}

std::vector<std::string> environmentCheckIds() {
    return {"jlink-software", "nrfutil",        "usb-access", "probe-holders",
            "probes",         "probe-firmware", "app-dirs",   "bluetooth"};
}

std::vector<Check> environmentChecks(const EnvironmentContext& context) {
    // The checks share the state and a copy of the context; std::function needs copyable lambdas.
    auto state = std::make_shared<SharedState>();
    auto ctx = std::make_shared<const EnvironmentContext>(context);
    std::vector<Check> checks;
    checks.push_back(Check{.id = "jlink-software",
                           .titleSourceText = jlinkTitle,
                           .timeout = milliseconds(5000),
                           .dependsOn = {},
                           .run = {}});
    checks.back().run = [check = checks.back(), ctx, state](const std::stop_token&) {
        return checkJLink(check, *ctx, *state);
    };
    checks.push_back(Check{.id = "nrfutil",
                           .titleSourceText = nrfUtilTitle,
                           .timeout = milliseconds(10000),
                           .dependsOn = {},
                           .run = {}});
    checks.back().run = [check = checks.back(), ctx](const std::stop_token&) {
        return checkNrfUtil(check, *ctx);
    };
    checks.push_back(Check{.id = "usb-access",
                           .titleSourceText = usbTitle,
                           .timeout = milliseconds(2000),
                           .dependsOn = {},
                           .run = {}});
    checks.back().run = [check = checks.back(), ctx](const std::stop_token&) {
        return checkUsb(check, *ctx);
    };
    checks.push_back(Check{.id = "probe-holders",
                           .titleSourceText = holdersTitle,
                           .timeout = processScanTimeout,
                           .dependsOn = {},
                           .run = {}});
    checks.back().run = [check = checks.back(), ctx](const std::stop_token& stop) {
        return checkHolders(check, *ctx, stop);
    };
    checks.push_back(Check{.id = "probes",
                           .titleSourceText = probesTitle,
                           .timeout = milliseconds(5000),
                           .dependsOn = {"jlink-software"},
                           .run = {}});
    checks.back().run = [check = checks.back(), ctx, state](const std::stop_token&) {
        return checkProbes(check, *ctx, *state);
    };
    // `probes` lists the probes it inspects (shared state), so it depends on it too (review V3).
    checks.push_back(Check{.id = "probe-firmware",
                           .titleSourceText = firmwareTitle,
                           .timeout = milliseconds(5000),
                           .dependsOn = {"jlink-software", "probes"},
                           .run = {}});
    checks.back().run = [check = checks.back(), ctx, state](const std::stop_token& stop) {
        return checkFirmware(check, *ctx, *state, stop);
    };
    checks.push_back(Check{.id = "app-dirs",
                           .titleSourceText = dirsTitle,
                           .timeout = milliseconds(5000),
                           .dependsOn = {},
                           .run = {}});
    checks.back().run = [check = checks.back(), ctx](const std::stop_token&) {
        return checkDirs(check, *ctx);
    };
    checks.push_back(Check{.id = "bluetooth",
                           .titleSourceText = bluetoothTitle,
                           .timeout = milliseconds(5000),
                           .dependsOn = {},
                           .run = {}});
    checks.back().run = [check = checks.back(), ctx](const std::stop_token&) {
        return checkBluetooth(check, *ctx);
    };
    return checks;
}

} // namespace hio::diag
