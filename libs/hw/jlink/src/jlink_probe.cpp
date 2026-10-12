#include "hio/hw/jlink/jlink_probe.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/hw/jlink/clock.h"
#include "hio/hw/jlink/jlink_api.h"
#include "hio/hw/jlink/types.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QString>
#include <QtLogging>

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hio::jlink {

namespace {

// J-Link command strings that keep the library from showing dialogs and from updating the probe
// firmware on its own (plan/features/0002: the firmware is never updated automatically; only
// `hio jlink update-firmware` updates it). Sources: SEGGER KB "J-Link Command Strings"
// (kb.segger.com/J-Link_Command_Strings) and the command names in the installed library 9.84.
//   DisableAutoUpdateFW      KB: "disable the automatic firmware update if a new firmware is
//                            available"; command name present in the library.
//   SuppressControlPanel     KB: control panel never pops up; "allowed to be used before
//                            JLINKARM_Open() was called".
//   SetBatchMode = 1         pylink disable_dialog_boxes(); command name present in the library.
//   HideDeviceSelection = 1  KB: no device selection dialog for an unknown device.
//   DisableInfoWinFlashDL    KB: no flash download information window.
// The library 9.84 accepts all of them before JLINKARM_Open (no error text; an unknown command is
// rejected), but the KB does not say that DisableAutoUpdateFW survives Open, so it is sent before
// AND right after Open (decided 2026-10-10). SilentUpdateFW ("update new firmware automatically")
// is never sent: it makes updates silent instead of disabling them.
constexpr std::array<std::string_view, 5> beforeOpenCommands{
    "DisableAutoUpdateFW", "SuppressControlPanel", "SetBatchMode = 1", "HideDeviceSelection = 1",
    "DisableInfoWinFlashDL"};
constexpr std::array<std::string_view, 4> afterOpenCommands{
    "DisableAutoUpdateFW", "SetBatchMode = 1", "HideDeviceSelection = 1", "DisableInfoWinFlashDL"};
// OpenMode::FirmwareUpdate (review R11): the dialog suppression without DisableAutoUpdateFW, before
// and after Open, so the explicit update of `hio jlink update-firmware` is not blocked.
constexpr std::array<std::string_view, 3> updateOpenCommands{
    "SetBatchMode = 1", "HideDeviceSelection = 1", "DisableInfoWinFlashDL"};

} // namespace

JLinkProbe::JLinkProbe(IJLinkApi& api, Clock& clock, ProbeTiming timing)
    : m_api(api), m_clock(clock), m_timing(timing) {}

std::string JLinkProbe::libraryDetail(std::string detail) {
    const std::string libraryText = m_api.takeErrorText();
    if (!libraryText.empty()) {
        detail += "; J-Link: " + libraryText;
    }
    return detail;
}

Result<std::vector<ProbeInfo>> JLinkProbe::list() {
    auto probes = m_api.listProbes(ProbeHosts::Usb);
    if (!probes) {
        return std::unexpected(makeError(
            ErrorCode::ProbeOpenFailed,
            libraryDetail(std::format("JLINKARM_EMU_GetList returned {}", probes.error()))));
    }
    return std::move(*probes);
}

Result<ProbeInfo> JLinkProbe::open(std::optional<std::uint32_t> serialNumber, OpenMode mode) {
    close();
    auto probes = list();
    if (!probes) {
        return std::unexpected(std::move(probes).error());
    }
    std::optional<ProbeInfo> chosen;
    if (serialNumber) {
        const auto it = std::ranges::find(*probes, *serialNumber, &ProbeInfo::serialNumber);
        if (it != probes->end()) {
            chosen = *it;
        }
    } else if (!probes->empty()) {
        chosen = probes->front();
    }
    if (!chosen) {
        Error error =
            makeError(ErrorCode::ProbeNotFound, std::format("{} probe(s) listed", probes->size()));
        if (serialNumber) {
            error.message =
                isNetworkProbe(*serialNumber)
                    ? QCoreApplication::translate("hio::jlink",
                                                  "The J-Link %1 is a network J-Link; network "
                                                  "J-Links are not supported for sessions yet.")
                          .arg(*serialNumber)
                          .toStdString()
                    : QCoreApplication::translate("hio::jlink",
                                                  "No J-Link probe with serial number %1 found.")
                          .arg(*serialNumber)
                          .toStdString();
        }
        return std::unexpected(std::move(error));
    }

    const int selected = m_api.selectBySerialNumber(chosen->serialNumber);
    if (selected < 0) {
        Error error =
            makeError(ErrorCode::ProbeNotFound,
                      libraryDetail(std::format("JLINKARM_EMU_SelectByUSBSN({}) returned {}",
                                                chosen->serialNumber, selected)));
        error.message = QCoreApplication::translate("hio::jlink",
                                                    "No J-Link probe with serial number %1 found.")
                            .arg(chosen->serialNumber)
                            .toStdString();
        return std::unexpected(std::move(error));
    }
    execCommands(mode == OpenMode::Normal ? std::span<const std::string_view>(beforeOpenCommands)
                                          : std::span<const std::string_view>(updateOpenCommands));
    if (const auto openError = m_api.open()) {
        return std::unexpected(makeError(
            ErrorCode::ProbeOpenFailed,
            libraryDetail(std::format("J-Link {}: {}", chosen->serialNumber, *openError))));
    }
    m_open = true;
    // Review R1: in every open mode, right after Open (pylink installs it there, too).
    // Without the hook a protected chip could be mass-erased by the library: do not continue.
    if (const int hooked = m_api.installUnsecureDialogHook(); hooked < 0) {
        close();
        return std::unexpected(makeError(
            ErrorCode::ProbeOpenFailed,
            libraryDetail(std::format("JLINK_SetHookUnsecureDialog returned {}", hooked))));
    }
    execCommands(mode == OpenMode::Normal ? std::span<const std::string_view>(afterOpenCommands)
                                          : std::span<const std::string_view>(updateOpenCommands));
    m_info = *chosen;
    m_info.firmware = m_api.firmwareString();
    m_api.flushMessages(); // a firmware-update message of Open is logged as a warning there
    qCInfo(log::lcJLink).noquote()
        << QStringLiteral("J-Link %1 opened (%2, firmware %3)")
               .arg(m_info.serialNumber)
               .arg(toQString(m_info.product), toQString(m_info.firmware));
    return m_info;
}

void JLinkProbe::execCommands(std::span<const std::string_view> commands) {
    for (const std::string_view command : commands) {
        const std::string result = m_api.execCommand(command);
        if (!result.empty()) {
            qCDebug(log::lcJLink).noquote()
                << "J-Link command" << toQString(command) << "->" << toQString(result);
        }
    }
}

void JLinkProbe::close() {
    if (m_open) {
        m_api.close();
        m_api.flushMessages();
        m_open = false;
        qCDebug(log::lcJLink) << "J-Link" << m_info.serialNumber << "closed";
    }
}

void JLinkProbe::setSpeed(int kiloHertz) {
    m_api.setSpeed(static_cast<std::uint32_t>(std::max(kiloHertz, 1)));
}

Status JLinkProbe::selectSwd() {
    const int result = m_api.selectInterface(TargetInterface::Swd);
    if (result != 0) {
        return std::unexpected(
            makeError(ErrorCode::ProbeOpenFailed,
                      libraryDetail(std::format("JLINKARM_TIF_Select(SWD) returned {}", result))));
    }
    return {};
}

Status JLinkProbe::connect(std::string_view deviceName, int minTargetMillivolts,
                           bool healthyVoltageSeen, const std::stop_token& stop) {
    const std::string deviceError = m_api.execCommand(std::format("Device = {}", deviceName));
    std::string lastFailure = deviceError.empty() ? std::string() : "Device: " + deviceError;
    for (int attempt = 1; attempt <= m_timing.connectAttempts; ++attempt) {
        if (stop.stop_requested()) {
            return fail(ErrorCode::Cancelled);
        }
        const int result = m_api.connect();
        if (result >= 0) {
            m_api.flushMessages();
            qCInfo(log::lcJLink).noquote()
                << "Connected to" << toQString(deviceName) << "on attempt" << attempt;
            return {};
        }
        lastFailure = std::format("JLINKARM_Connect returned {} (attempt {} of {})", result,
                                  attempt, m_timing.connectAttempts);
        qCInfo(log::lcJLink).noquote() << toQString(lastFailure);
        if (attempt < m_timing.connectAttempts &&
            !m_clock.sleepFor(m_timing.connectRetryDelay, stop)) {
            return fail(ErrorCode::Cancelled);
        }
    }

    // Cable diagnosis: a low target voltage means power / flat cable. 0 mV is trusted only after a
    // healthy reading on this probe: a probe that cannot measure VTref always reports 0 and must
    // never turn a failed connect into target.no_power (ADR-0034; review R21).
    const auto status = m_api.hardwareStatus();
    const bool lowVoltage = status && status->targetMillivolts < minTargetMillivolts &&
                            (status->targetMillivolts > 0 || healthyVoltageSeen);
    if (lowVoltage) {
        Error error = makeError(
            ErrorCode::TargetNoPower,
            libraryDetail(std::format("{}; VTref {} mV", lastFailure, status->targetMillivolts)));
        error.message =
            QCoreApplication::translate("hio::jlink", "The device has no power (%1 mV).")
                .arg(status->targetMillivolts)
                .toStdString();
        return std::unexpected(std::move(error));
    }
    Error error = makeError(
        ErrorCode::TargetConnectFailed,
        libraryDetail(status ? std::format("{}; VTref {} mV", lastFailure, status->targetMillivolts)
                             : lastFailure));
    error.message =
        QCoreApplication::translate("hio::jlink", "Could not connect to the device's %1 MCU.")
            .arg(toQString(deviceName))
            .toStdString();
    return std::unexpected(std::move(error));
}

Result<HardwareStatus> JLinkProbe::hardwareStatus() {
    auto status = m_api.hardwareStatus();
    if (!status) {
        return std::unexpected(
            makeError(ErrorCode::ProbeNotFound, libraryDetail("JLINKARM_GetHWStatus failed")));
    }
    return *status;
}

FirmwareStatus JLinkProbe::firmwareStatus() {
    const std::string firmware = m_api.firmwareString();
    const auto embedded = m_api.embeddedFirmwareString(firmwareId(firmware));
    if (!embedded) {
        qCInfo(log::lcJLink).noquote()
            << "J-Link firmware status unknown: GetEmbeddedFWString returned" << embedded.error();
        return FirmwareStatus::Unknown;
    }
    const auto status = compareFirmware(firmware, *embedded);
    if (!status) {
        qCInfo(log::lcJLink).noquote() << "J-Link firmware status unknown: cannot compare"
                                       << toQString(firmware) << "with" << toQString(*embedded);
        return FirmwareStatus::Unknown;
    }
    qCInfo(log::lcJLink).noquote()
        << "J-Link firmware" << toQString(firmware) << "library" << toQString(*embedded) << "->"
        << toQString(firmwareStatusId(*status));
    return *status;
}

Result<FirmwareStatus> JLinkProbe::updateFirmware() {
    // "before" is read after a Normal open (automatic update disabled before and after Open), so
    // "up to date" never comes from an Open that may have updated the probe itself (review R24).
    const FirmwareStatus before = firmwareStatus();
    if (before == FirmwareStatus::Current) {
        return before;
    }
    const std::uint32_t serial = m_info.serialNumber;
    close();
    if (auto reopened = open(serial, OpenMode::FirmwareUpdate); !reopened) {
        return std::unexpected(std::move(reopened).error());
    }
    if (before == FirmwareStatus::Newer) {
        // pylink sync_firmware(): invalidate, so the library downloads its own firmware.
        const std::string result = m_api.execCommand("InvalidateFW");
        if (!result.empty()) {
            qCInfo(log::lcJLink).noquote() << "InvalidateFW:" << toQString(result);
        }
    }
    const std::uint32_t checksum = m_api.updateFirmwareIfNewer();
    m_api.flushMessages();
    qCInfo(log::lcJLink) << "J-Link firmware update finished, checksum" << checksum;

    // Verify (review R11): open the probe again normally and re-read the status. pylink's
    // sync_firmware() reopens right after the update, too.
    close();
    if (auto reopened = open(serial, OpenMode::Normal); !reopened) {
        return std::unexpected(std::move(reopened).error());
    }
    const FirmwareStatus after = firmwareStatus();
    if (after != FirmwareStatus::Current) {
        return std::unexpected(makeError(
            ErrorCode::ProbeFirmwareUpdateFailed,
            libraryDetail(std::format("J-Link {}: firmware {} before the update, {} after it ({})",
                                      serial, firmwareStatusId(before), firmwareStatusId(after),
                                      m_info.firmware))));
    }
    return before;
}

bool JLinkProbe::isNetworkProbe(std::uint32_t serialNumber) {
    // Listing network J-Links scans the network: done once per serial number and JLinkProbe
    // (= once per session, or per tool run), never on every auto-reconnect retry (review R22).
    if (m_networkCheck && m_networkCheck->first == serialNumber) {
        return m_networkCheck->second;
    }
    auto all = m_api.listProbes(ProbeHosts::UsbAndIp);
    const bool network =
        all && std::ranges::any_of(*all, [serialNumber](const ProbeInfo& probe) {
            return probe.serialNumber == serialNumber && probe.connection == Connection::Ip;
        });
    m_networkCheck = std::pair(serialNumber, network);
    return network;
}

Status JLinkProbe::reset(bool halt) {
    m_api.setResetDelay(m_timing.resetDelayMs);
    const int result = m_api.reset();
    if (result < 0) {
        return std::unexpected(
            makeError(ErrorCode::TargetConnectFailed,
                      libraryDetail(std::format("JLINKARM_Reset returned {}", result))));
    }
    if (!halt) {
        m_api.go();
    }
    m_api.flushMessages();
    return {};
}

Status JLinkProbe::halt() {
    const int result = m_api.halt();
    if (result != 0) {
        return std::unexpected(
            makeError(ErrorCode::TargetConnectFailed,
                      libraryDetail(std::format("JLINKARM_Halt returned {}", result))));
    }
    return {};
}

void JLinkProbe::go() { m_api.go(); }

} // namespace hio::jlink
