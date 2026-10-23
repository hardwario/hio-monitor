#pragma once

// SessionProvider - an IDeviceProvider with real sessions for the tool tests (HIO_TEST_OVERRIDES
// builds only): DeviceSessions on the test-only fake J-Link library (tests/fake_jlink, scenario in
// HIO_FAKE_JLINK_SCENARIO) and BleSessions on the fake BLE backend (tests/ble_scenarios). Ids
// "jlink:<sn>" and "ble:<id>" (ADR-0098). Not the CLI's CliDeviceProvider (C6).

#include "fake/fake_ble_api.h"
#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/ble_session.h"
#include "hio/device/console_log.h"
#include "hio/device/console_session.h"
#include "hio/device/device_session.h"
#include "hio/device/profile.h"
#include "hio/device/session_registry.h"
#include "hio/hw/ble/ble_api.h"
#include "hio/hw/ble/ble_host.h"
#include "hio/hw/jlink/library_locator.h"
#include "hio/mcp/request_context.h"
#include "hio/mcp/tools/device_provider.h"
#include "tools_test_support.h"

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <QtEnvironmentVariables>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hio::mcp::tools::test {

class SessionProvider final : public IDeviceProvider {
public:
    // Options of the next session and what list() adds.
    struct Settings {
        std::filesystem::path library = HIO_FAKE_JLINK_LIBRARY;
        const hio::device::DeviceProfile* profile = &chesterProfile();
        std::size_t logCapacity = hio::device::ConsoleLog::defaultCapacityPerChannel;
        // The BLE scenario file (tests/ble_scenarios/<name>).
        std::filesystem::path bleScenario =
            std::filesystem::path(HIO_BLE_SCENARIOS_DIR) / "chester.json";
        std::vector<DeviceEntry> available; // listed besides the open sessions
    };

    [[nodiscard]] Settings& settings() { return m_settings; }
    [[nodiscard]] ProviderRecord& record() { return m_record; }
    [[nodiscard]] int refreshes() const { return m_refreshes; }

    SessionProvider()
        : m_registry(std::make_unique<hio::device::SessionRegistry>(
              hio::device::RegistryOptions{.instanceRoot = root() / "instances",
                                           .lockDirectory = root() / "locks",
                                           .consoleDirectory = root() / "console"})) {}
    ~SessionProvider() override {
        m_sessions.clear();
        if (m_host) {
            m_host->stop();
        }
        qunsetenv("HIO_FAKE_JLINK_SCENARIO");
    }
    SessionProvider(const SessionProvider&) = delete;
    SessionProvider& operator=(const SessionProvider&) = delete;
    SessionProvider(SessionProvider&&) = delete;
    SessionProvider& operator=(SessionProvider&&) = delete;

    [[nodiscard]] std::filesystem::path root() const { return {m_dir.path().toStdString()}; }

    // Writes the fake J-Link scenario and points HIO_FAKE_JLINK_SCENARIO at it.
    void scenario(const QByteArray& json) const {
        const QString path = m_dir.filePath(QStringLiteral("scenario.json"));
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(json);
        }
        file.close();
        qputenv("HIO_FAKE_JLINK_SCENARIO", path.toUtf8());
    }

    // Opens a session directly (as if a GUI tab were opened by the user).
    hio::Result<DeviceRef> openNow(const std::string& id) {
        std::optional<hio::Result<DeviceRef>> out;
        OpenRequest request;
        request.id = id;
        open(request, [&out](hio::Result<DeviceRef> ref) { out = std::move(ref); });
        return out.value_or(std::unexpected(makeError(ErrorCode::InternalUnexpected)));
    }

    // The log of an open session (the provider owns it, like the CLI provider).
    [[nodiscard]] std::shared_ptr<hio::device::ConsoleLog> logOf(const std::string& id) const {
        const auto it = m_sessions.find(id);
        return it == m_sessions.end() ? nullptr : it->second.log;
    }

    std::vector<DeviceEntry> list() override {
        std::vector<DeviceEntry> entries;
        entries.reserve(m_sessions.size() + m_settings.available.size());
        for (const auto& [id, entry] : m_sessions) {
            entries.push_back(DeviceEntry{.id = id,
                                          .kind = id.starts_with("ble:") ? "ble" : "jlink",
                                          .displayName = entry.session->identity().displayName,
                                          .profileId = std::string(entry.session->profile().id),
                                          .state = DeviceEntry::State::Open,
                                          .owner = std::nullopt,
                                          .connection = entry.session->connection(),
                                          .openedByMcp = true});
        }
        for (const DeviceEntry& entry : m_settings.available) {
            entries.push_back(entry);
        }
        return entries;
    }

    // Asynchronous like the real providers (ADR-0162): `done` follows on a later event-loop turn.
    void refresh(std::function<void(hio::Status)> done) override {
        ++m_refreshes;
        QTimer::singleShot(0, &m_notifier, [done = std::move(done)] { done(hio::Status{}); });
    }

    void open(const OpenRequest& request,
              std::function<void(hio::Result<DeviceRef>)> done) override {
        if (auto existing = find(request.id)) {
            done(*existing);
            return;
        }
        auto log = std::make_shared<hio::device::ConsoleLog>(m_settings.logCapacity);
        std::unique_ptr<hio::device::ConsoleSession> session;
        hio::device::DeviceSession* jlink = nullptr;
        if (request.id.starts_with("jlink:")) {
            hio::device::SessionOptions options;
            options.profile = m_settings.profile;
            options.targetId = request.targetId.empty() ? std::string_view("app")
                                                        : std::string_view(request.targetId);
            options.probeSn = static_cast<std::uint32_t>(std::stoul(request.id.substr(6)));
            options.speedKhz = request.speedKhz.value_or(2000);
            options.latencyMs = 5;
            options.autoReconnect = request.autoReconnect.value_or(false);
            options.consoleFile = std::filesystem::path();
            options.consoleLog = log;
            auto opened = hio::device::DeviceSession::open(
                *m_registry, hio::jlink::LibraryLocation{.path = m_settings.library},
                std::move(options));
            if (!opened) {
                done(std::unexpected(std::move(opened).error()));
                return;
            }
            jlink = opened->get();
            session = std::move(*opened);
        } else if (request.id.starts_with("ble:")) {
            if (!m_host) {
                m_host = std::make_unique<hio::ble::BleHost>(
                    [file = m_settings.bleScenario]() -> std::unique_ptr<hio::ble::IBleApi> {
                        auto loaded = hio::ble::loadFakeScenario(file);
                        return loaded ? std::make_unique<hio::ble::FakeBleApi>(std::move(*loaded))
                                      : nullptr;
                    });
            }
            auto opened = hio::device::BleSession::open(
                *m_registry, *m_host,
                hio::device::BleSessionOptions{.profile = &chesterProfile(),
                                               .deviceId = request.id.substr(4),
                                               .deviceName = "CHESTER-ABC123",
                                               .autoReconnect = false,
                                               .consoleFile = std::filesystem::path(),
                                               .consoleLog = log});
            if (!opened) {
                done(std::unexpected(std::move(opened).error()));
                return;
            }
            session = std::move(*opened);
        } else {
            done(std::unexpected(makeError(ErrorCode::McpDeviceNotOpen, request.id)));
            return;
        }
        const DeviceRef ref{.console = session.get(), .jlink = jlink};
        m_sessions[request.id] = Entry{.session = std::move(session), .log = log};
        emit m_notifier.devicesChanged();
        done(ref);
    }

    hio::Status close(std::string_view id, const hio::mcp::RequestContext& /*context*/) override {
        const auto it = m_sessions.find(std::string(id));
        if (it == m_sessions.end()) {
            return std::unexpected(makeError(ErrorCode::McpDeviceNotOpen, std::string(id)));
        }
        auto entry = std::move(it->second);
        m_sessions.erase(it);
        entry.session.reset(); // closes (the destructor)
        emit m_notifier.devicesChanged();
        return {};
    }

    std::optional<DeviceRef> find(std::string_view id) override {
        const auto it = m_sessions.find(std::string(id));
        if (it == m_sessions.end()) {
            return std::nullopt;
        }
        return DeviceRef{.console = it->second.session.get(),
                         .jlink =
                             dynamic_cast<hio::device::DeviceSession*>(it->second.session.get())};
    }

    void beginBusy(std::string_view id, QString text, std::function<void()> cancel) override {
        m_record.busy.emplace_back(id, std::move(text));
        m_record.busyCancel = std::move(cancel);
    }
    void endBusy(std::string_view /*id*/) override { ++m_record.endBusy; }
    void marker(std::string_view id, QString text) override {
        if (const auto it = m_sessions.find(std::string(id)); it != m_sessions.end()) {
            it->second.log->appendMarker(hio::device::Channel::Shell, text.toStdString());
        }
        m_record.markers.emplace_back(id, std::move(text));
    }
    void setAiCommandHint(std::string_view id, bool active) override {
        m_record.hints.emplace_back(id, active);
    }
    DeviceProviderNotifier& notifier() override { return m_notifier; }

private:
    struct Entry {
        std::unique_ptr<hio::device::ConsoleSession> session;
        std::shared_ptr<hio::device::ConsoleLog> log;
    };

    QTemporaryDir m_dir;
    std::unique_ptr<hio::device::SessionRegistry> m_registry;
    std::unique_ptr<hio::ble::BleHost> m_host;
    std::map<std::string, Entry> m_sessions;
    DeviceProviderNotifier m_notifier;
    Settings m_settings;
    ProviderRecord m_record;
    int m_refreshes = 0;
};

} // namespace hio::mcp::tools::test
