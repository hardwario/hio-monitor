#include "operation_runner.h"

#include "cli_app.h"
#include "cli_log.h"
#include "hio/core/result.h"
#include "hio/device/device_session.h"
#include "hio/device/operation.h"
#include "hio/device/profile.h"
#include "hio/hw/jlink/types.h"
#include "hio/hw/nrfutil/nrfutil.h"
#include "jlink_commands.h"
#include "probe_options.h"
#include "progress_ui.h"

#include <QLoggingCategory>

#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace hio::cli {

namespace {

constexpr std::string_view profileId = "chester";

} // namespace

const device::DeviceProfile& chesterProfile() { return *device::ProfileRegistry::find(profileId); }

const device::TargetProfile& chesterTarget(std::string_view targetId) {
    return *device::findTarget(chesterProfile(), targetId);
}

OperationRunner::OperationRunner(Passkey /*key*/, const Context& context, ProgressUi& ui,
                                 std::string_view targetId)
    : m_context(&context), m_ui(&ui), m_targetId(targetId) {}

OperationRunner::~OperationRunner() { close(); }

Result<std::unique_ptr<OperationRunner>>
OperationRunner::open(const Context& context, const GlobalOptions& globals,
                      const ProbeOptions& group, const ProbeOptions& own, std::string_view targetId,
                      ProgressUi& ui, std::optional<std::filesystem::path> nrfUtil) {
    auto location = locateJLinkLibrary(globals);
    if (!location) {
        return std::unexpected(std::move(location).error());
    }
    const device::TargetProfile& target = chesterTarget(targetId);
    device::SessionOptions options;
    options.profile = &chesterProfile();
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
    auto runner = std::make_unique<OperationRunner>(Passkey{}, context, ui, targetId);
    auto session = device::DeviceSession::open(runner->m_registry, *location, std::move(options));
    if (!session) {
        return std::unexpected(std::move(session).error());
    }
    runner->m_session = std::move(*session);
    return runner;
}

void OperationRunner::announce() {
    if (!m_announced && m_session->probe().serialNumber != 0) {
        m_announced = true;
        const jlink::ProbeInfo probe = m_session->probe();
        qCInfo(lcCli) << "Using J-Link" << probe.serialNumber;
        m_ui->info(probe.product.empty()
                       ? std::format("Using J-Link {}", probe.serialNumber)
                       : std::format("Using J-Link {} ({})", probe.serialNumber, probe.product));
    }
}

Result<device::OperationResult> OperationRunner::run(const StartFn& start) {
    device::DeviceSession& device = *m_session;
    auto id = start(device);
    if (!id) {
        device.close();
        return std::unexpected(std::move(id).error());
    }
    device::OperationResult result = device::waitForOperation(
        device, *id,
        [this](const device::OperationProgress& progress) {
            announce();
            m_ui->progress(progress);
        },
        m_context->stop);
    announce();
    m_ui->finish();
    m_probeSerial = device.probe().serialNumber;
    if (!result.status) {
        device.close(); // releases the probe (and its lock) now; joins the worker's current step
        // Review R13: when the wait gave up after Ctrl+C, the worker may still have finished the
        // operation (a recover, an erase-all, or a real error such as a changed PIB): report that.
        if (auto real = device.finishedResult(*id)) {
            result = std::move(*real);
        }
        if (!result.status) {
            return std::unexpected(result.status.error());
        }
    }
    return result;
}

void OperationRunner::close() {
    if (m_session) {
        m_session->close();
    }
}

void OperationRunner::showTarget(const device::OperationResult& result) {
    if (result.chip && !result.chip->name.empty()) {
        m_ui->info(std::format("Target: {} ({})", result.chip->name, m_targetId));
    }
}

Result<OperationOutcome> runOperation(const Context& context, const GlobalOptions& globals,
                                      const ProbeOptions& group, const ProbeOptions& own,
                                      std::string_view targetId, ProgressUi& ui,
                                      const StartFn& start,
                                      std::optional<std::filesystem::path> nrfUtil) {
    auto runner =
        OperationRunner::open(context, globals, group, own, targetId, ui, std::move(nrfUtil));
    if (!runner) {
        return std::unexpected(std::move(runner).error());
    }
    auto result = (*runner)->run(start);
    (*runner)->close();
    if (!result) {
        return std::unexpected(std::move(result).error());
    }
    (*runner)->showTarget(*result);
    return OperationOutcome{.result = std::move(*result), .probeSerial = (*runner)->probeSerial()};
}

} // namespace hio::cli
