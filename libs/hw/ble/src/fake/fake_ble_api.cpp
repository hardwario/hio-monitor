#include "fake_ble_api.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/hw/ble/ble_api.h"
#include "hio/hw/ble/types.h"

#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QtCore/qnamespace.h>
#include <QtLogging>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hio::ble {

namespace {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): a process-wide test counter
std::atomic<int> threadViolations{0};

constexpr int pairingResultDelayMs = 100;

std::optional<ErrorCode> errorCodeOf(const QJsonObject& object, const QString& key) {
    const QString id = object.value(key).toString();
    if (id.isEmpty()) {
        return std::nullopt;
    }
    return errorCodeFromId(id.toStdString()).value_or(ErrorCode::InternalUnexpected);
}

AdapterState adapterStateOf(const QString& state) {
    if (state == QStringLiteral("notFound")) {
        return AdapterState::NotFound;
    }
    if (state == QStringLiteral("poweredOff")) {
        return AdapterState::PoweredOff;
    }
    if (state == QStringLiteral("permissionDenied")) {
        return AdapterState::PermissionDenied;
    }
    if (state == QStringLiteral("permissionUndetermined")) {
        return AdapterState::PermissionUndetermined;
    }
    if (state == QStringLiteral("stackUnavailable")) {
        return AdapterState::StackUnavailable;
    }
    return AdapterState::Ready;
}

FakeDevice deviceOf(const QJsonObject& object) {
    FakeDevice device;
    device.id = object.value(QStringLiteral("id")).toString().toStdString();
    device.name = object.value(QStringLiteral("name")).toString().toStdString();
    device.rssi = object.value(QStringLiteral("rssi")).toInt(device.rssi);
    device.nus = object.value(QStringLiteral("nus")).toBool(device.nus);
    device.advertise = object.value(QStringLiteral("advertise")).toBool(device.advertise);
    device.paired = object.value(QStringLiteral("paired")).toBool(device.paired);
    device.pairing =
        object.value(QStringLiteral("pairing")).toString(QStringLiteral("ok")).toStdString();
    device.connectDelayMs =
        object.value(QStringLiteral("connectDelayMs")).toInt(device.connectDelayMs);
    device.connectError = errorCodeOf(object, QStringLiteral("connectError"));
    device.connectErrorOnce = object.value(QStringLiteral("connectErrorOnce")).toBool();
    device.pairingDelayMs = object.value(QStringLiteral("pairingDelayMs")).toInt();
    device.mtu = static_cast<std::uint16_t>(object.value(QStringLiteral("mtu")).toInt(247));
    device.writeWithoutResponse = object.value(QStringLiteral("writeWithoutResponse")).toBool();
    device.writeStall = object.value(QStringLiteral("writeStall")).toBool();
    device.writeError = errorCodeOf(object, QStringLiteral("writeError"));
    device.echo = object.value(QStringLiteral("echo")).toBool();
    device.newlineAfterCommand = object.value(QStringLiteral("newlineAfterCommand")).toBool();
    device.prompt = object.value(QStringLiteral("prompt")).toString().toStdString();
    device.greeting = object.value(QStringLiteral("greeting")).toString().toStdString();
    device.promptOnConnect = object.value(QStringLiteral("promptOnConnect")).toBool(true);
    const QJsonObject responses = object.value(QStringLiteral("responses")).toObject();
    for (auto it = responses.begin(); it != responses.end(); ++it) {
        device.responses.emplace(it.key().toStdString(), it.value().toString().toStdString());
    }
    for (const auto& value : object.value(QStringLiteral("output")).toArray()) {
        const QJsonObject output = value.toObject();
        device.output.push_back(
            FakeOutput{.atMs = output.value(QStringLiteral("atMs")).toInt(),
                       .text = output.value(QStringLiteral("text")).toString().toStdString()});
    }
    device.dropAfterMs = object.value(QStringLiteral("dropAfterMs")).toInt();
    device.adapterOffAfterDrop = object.value(QStringLiteral("adapterOffAfterDrop")).toBool();
    return device;
}

} // namespace

int fakeBleThreadViolations() noexcept { return threadViolations.load(); }

Result<FakeScenario> loadFakeScenario(const std::filesystem::path& file) {
    QFile reader(toQString(toUtf8(file)));
    if (!reader.open(QIODevice::ReadOnly)) {
        return std::unexpected(makeError(ErrorCode::IoFileNotFound, toUtf8(file)));
    }
    const QJsonDocument document = QJsonDocument::fromJson(reader.readAll());
    if (!document.isObject()) {
        return std::unexpected(
            makeError(ErrorCode::IoReadFailed, toUtf8(file) + ": not a JSON object"));
    }
    const QJsonObject root = document.object();
    FakeScenario scenario;
    const QJsonObject adapter = root.value(QStringLiteral("adapter")).toObject();
    scenario.adapter.state = adapterStateOf(adapter.value(QStringLiteral("state")).toString());
    scenario.adapter.name =
        adapter.value(QStringLiteral("name")).toString(QStringLiteral("fake0")).toStdString();
    scenario.adapter.address = adapter.value(QStringLiteral("address")).toString().toStdString();
    if (root.contains(QStringLiteral("adapterLater"))) {
        const QJsonObject later = root.value(QStringLiteral("adapterLater")).toObject();
        scenario.adapterLater =
            AdapterInfo{.state = adapterStateOf(later.value(QStringLiteral("state")).toString()),
                        .name = scenario.adapter.name,
                        .address = scenario.adapter.address};
    }
    scenario.pairingSupported = root.value(QStringLiteral("pairingSupported")).toBool(true);
    scenario.scanError = errorCodeOf(root, QStringLiteral("scanError"));
    scenario.advertiseIntervalMs =
        root.value(QStringLiteral("advertiseIntervalMs")).toInt(scenario.advertiseIntervalMs);
    for (const auto& value : root.value(QStringLiteral("devices")).toArray()) {
        scenario.devices.push_back(deviceOf(value.toObject()));
    }
    const QJsonObject generate = root.value(QStringLiteral("generate")).toObject();
    const int count = generate.value(QStringLiteral("count")).toInt();
    const std::string prefix =
        generate.value(QStringLiteral("namePrefix")).toString(QStringLiteral("GEN-")).toStdString();
    const int rssi = generate.value(QStringLiteral("rssi")).toInt(-80);
    for (int i = 0; i < count; ++i) {
        FakeDevice device;
        device.id = std::format("C0:00:00:00:{:02X}:{:02X}", (i >> 8) & 0xFF, i & 0xFF);
        device.name = std::format("{}{:04}", prefix, i);
        device.rssi = rssi - (i % 20);
        scenario.devices.push_back(std::move(device));
    }
    return scenario;
}

FakeBleApi::FakeBleApi(FakeScenario scenario)
    : m_scenario(std::move(scenario)), m_thread(QThread::currentThread()),
      m_context(std::make_unique<QObject>()) {
    qCInfo(log::lcBle) << "Using the fake Bluetooth backend (" << m_scenario.devices.size()
                       << "devices)";
}

FakeBleApi::~FakeBleApi() {
    checkThread();
    m_links.clear();
    m_scanContext.reset();
    m_context.reset();
}

void FakeBleApi::checkThread() const {
    if (QThread::currentThread() != m_thread) {
        threadViolations.fetch_add(1);
        qCCritical(log::lcBle) << "FakeBleApi used on another thread";
    }
}

void FakeBleApi::setListener(IBleListener* listener) {
    checkThread();
    m_listener = listener;
}

AdapterInfo FakeBleApi::adapter() {
    checkThread();
    if (++m_adapterQueries > 1 && m_scenario.adapterLater) {
        m_scenario.adapter = *m_scenario.adapterLater;
        m_scenario.adapterLater.reset();
    }
    return m_scenario.adapter;
}

FakeDevice* FakeBleApi::device(const BleDeviceId& id) {
    for (FakeDevice& item : m_scenario.devices) {
        if (sameDeviceId(item.id, id)) {
            return &item;
        }
    }
    return nullptr;
}

FakeBleApi::Link* FakeBleApi::find(LinkId id) {
    const auto it = m_links.find(id);
    return it == m_links.end() ? nullptr : &it->second;
}

Status FakeBleApi::startScan(std::chrono::milliseconds duration) {
    checkThread();
    if (auto problem = adapterError(m_scenario.adapter)) {
        return std::unexpected(std::move(*problem));
    }
    if (m_scenario.scanError) {
        return std::unexpected(makeError(*m_scenario.scanError, "fake scan error"));
    }
    m_scanContext = std::make_unique<QObject>();
    QTimer::singleShot(std::chrono::milliseconds(10), m_scanContext.get(), [this] { advertise(); });
    if (duration.count() > 0) {
        QTimer::singleShot(duration, m_scanContext.get(), [this] {
            m_scanContext.reset();
            if (m_listener != nullptr) {
                m_listener->onScanFinished(std::nullopt);
            }
        });
    }
    return {};
}

void FakeBleApi::advertise() {
    if (!m_scanContext || m_listener == nullptr) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    for (const FakeDevice& item : m_scenario.devices) {
        if (!item.advertise) {
            continue;
        }
        Advertisement advertisement{
            .id = item.id, .name = item.name, .rssi = item.rssi, .serviceUuids = {}, .seen = now};
        if (item.nus) {
            advertisement.serviceUuids.emplace_back(nusServiceUuid);
        }
        m_listener->onAdvertisement(advertisement);
        if (!m_scanContext) {
            return; // stopped by the listener
        }
    }
    QTimer::singleShot(std::chrono::milliseconds(m_scenario.advertiseIntervalMs),
                       m_scanContext.get(), [this] { advertise(); });
}

void FakeBleApi::stopScan() {
    checkThread();
    m_scanContext.reset();
}

Result<LinkId> FakeBleApi::connect(const BleDeviceId& deviceId, const NusUuids& /*uuids*/) {
    checkThread();
    const LinkId id = m_nextLink++;
    Link link{
        .id = id, .device = 0, .context = std::make_unique<QObject>(), .input = {}, .ready = false};
    const FakeDevice* item = device(deviceId);
    if (item == nullptr) {
        QTimer::singleShot(std::chrono::milliseconds(20), link.context.get(), [this, id] {
            m_links.erase(id);
            if (m_listener != nullptr) {
                m_listener->onLinkLost(
                    id, makeError(ErrorCode::BleConnectFailed, "fake: unknown device"));
            }
        });
        m_links.emplace(id, std::move(link));
        return id;
    }
    link.device = static_cast<std::size_t>(item - m_scenario.devices.data());
    QTimer::singleShot(std::chrono::milliseconds(item->connectDelayMs), link.context.get(),
                       [this, id] { startLink(id); });
    m_links.emplace(id, std::move(link));
    return id;
}

void FakeBleApi::startLink(LinkId id) {
    Link* link = find(id);
    if (link == nullptr || m_listener == nullptr) {
        return;
    }
    const FakeDevice& item = m_scenario.devices.at(link->device);
    const int attempt = ++m_attempts[link->device];
    if (item.connectError && (!item.connectErrorOnce || attempt == 1)) {
        const ErrorCode code = *item.connectError;
        m_links.erase(id);
        m_listener->onLinkLost(id, makeError(code, "fake connect error"));
        return;
    }
    m_listener->onLinkConnecting(id, true); // the CCCD write starts
    QTimer::singleShot(std::chrono::milliseconds(item.pairingDelayMs), link->context.get(),
                       [this, id] { linkReady(id); });
}

void FakeBleApi::linkReady(LinkId id) {
    Link* link = find(id);
    if (link == nullptr || m_listener == nullptr) {
        return;
    }
    const std::size_t index = link->device;
    FakeDevice& item = m_scenario.devices.at(index);
    item.paired = true; // bonded by connecting
    link->ready = true;
    m_listener->onLinkReady(id, LinkReady{.mtu = item.mtu,
                                          .writeMode = item.writeWithoutResponse
                                                           ? WriteMode::WithoutResponse
                                                           : WriteMode::WithResponse});
    if (find(id) == nullptr) {
        return;
    }
    send(id, item.greeting + (item.promptOnConnect ? item.prompt : std::string()));
    for (const FakeOutput& output : item.output) {
        QTimer::singleShot(std::chrono::milliseconds(output.atMs), link->context.get(),
                           [this, id, text = output.text] { send(id, text); });
    }
    if (item.dropAfterMs > 0 && !m_dropped[index]) {
        m_dropped[index] = true;
        QTimer::singleShot(std::chrono::milliseconds(item.dropAfterMs), link->context.get(),
                           [this, id, off = item.adapterOffAfterDrop] {
                               if (off) {
                                   m_scenario.adapter.state = AdapterState::PoweredOff;
                               }
                               m_links.erase(id);
                               if (m_listener != nullptr) {
                                   m_listener->onLinkLost(
                                       id, makeError(ErrorCode::BleDisconnected, "fake drop"));
                               }
                           });
    }
}

void FakeBleApi::send(LinkId id, const std::string& text) {
    if (find(id) == nullptr || m_listener == nullptr || text.empty()) {
        return;
    }
    // Notifications of at most MTU - 3 bytes, like a real link.
    const auto payload =
        static_cast<std::size_t>(m_scenario.devices.at(find(id)->device).mtu - attWriteHeader);
    for (std::size_t offset = 0; offset < text.size(); offset += payload) {
        const std::string_view part = std::string_view(text).substr(offset, payload);
        m_listener->onLinkData(id, std::as_bytes(std::span(part.data(), part.size())));
        if (find(id) == nullptr) {
            return;
        }
    }
}

void FakeBleApi::received(LinkId id, std::string_view bytes) {
    Link* link = find(id);
    if (link == nullptr) {
        return;
    }
    const FakeDevice& item = m_scenario.devices.at(link->device);
    for (const char c : bytes) {
        if (c != '\n') {
            link->input += c;
            continue;
        }
        std::string line = std::move(link->input);
        link->input.clear();
        if (line.ends_with('\r')) {
            line.pop_back();
        }
        std::string reply;
        if (item.echo) {
            reply += line + "\r\n";
        } else if (item.newlineAfterCommand) {
            // HIL H5: CHESTER moves to a new line after Enter at once (about 13 ms after the
            // send), in its own notification and with no prompt before it.
            answer(*link, std::chrono::milliseconds(5), "\r\n");
        }
        if (const auto it = item.responses.find(line); it != item.responses.end()) {
            reply += it->second;
        }
        reply += item.prompt;
        // Like a device: the answer comes a little later, after the write was confirmed.
        answer(*link, std::chrono::milliseconds(15), std::move(reply));
    }
}

void FakeBleApi::answer(Link& link, std::chrono::milliseconds delay, std::string text) {
    const auto now = std::chrono::steady_clock::now();
    link.nextAnswer = std::max(now + delay, link.nextAnswer);
    // Qt fires timers with the same deadline in the order they were started.
    QTimer::singleShot(std::chrono::ceil<std::chrono::milliseconds>(link.nextAnswer - now),
                       Qt::PreciseTimer, link.context.get(),
                       [this, id = link.id, text = std::move(text)] { send(id, text); });
}

Status FakeBleApi::write(LinkId id, std::span<const std::byte> chunk, WriteMode /*mode*/,
                         std::uint64_t token) {
    checkThread();
    Link* link = find(id);
    if (link == nullptr || !link->ready) {
        return std::unexpected(makeError(ErrorCode::BleWriteFailed, "fake: no link"));
    }
    const FakeDevice& item = m_scenario.devices.at(link->device);
    if (item.writeStall) {
        return {};
    }
    if (item.writeError) {
        const ErrorCode code = *item.writeError;
        QTimer::singleShot(
            std::chrono::milliseconds(1), link->context.get(), [this, id, token, code] {
                if (m_listener != nullptr) {
                    m_listener->onWriteDone(id, token, makeError(code, "fake write error"));
                }
            });
        return {};
    }
    std::string bytes;
    bytes.reserve(chunk.size());
    for (const std::byte b : chunk) {
        bytes.push_back(static_cast<char>(b));
    }
    QTimer::singleShot(std::chrono::milliseconds(1), link->context.get(),
                       [this, id, token, bytes = std::move(bytes)] {
                           if (m_listener != nullptr) {
                               m_listener->onWriteDone(id, token, std::nullopt);
                           }
                           received(id, bytes);
                       });
    return {};
}

void FakeBleApi::disconnect(LinkId id) {
    checkThread();
    m_links.erase(id); // its context cancels the pending timers
}

Status FakeBleApi::requestPairing(const BleDeviceId& deviceId) {
    checkThread();
    if (!m_scenario.pairingSupported) {
        return std::unexpected(makeError(ErrorCode::BlePairingNotSupported, "fake macOS"));
    }
    FakeDevice* item = device(deviceId);
    if (item == nullptr) {
        return std::unexpected(makeError(ErrorCode::BleDeviceNotFound, deviceId));
    }
    if (item->pairing == "never") {
        return {};
    }
    const bool ok = item->pairing == "ok";
    const ErrorCode failure =
        item->pairing == "noAgent" ? ErrorCode::BleNoPairingAgent : ErrorCode::BlePairingFailed;
    QTimer::singleShot(
        std::chrono::milliseconds(pairingResultDelayMs), m_context.get(),
        [this, deviceId, ok, failure] {
            if (ok) {
                if (FakeDevice* paired = device(deviceId)) {
                    paired->paired = true;
                }
            }
            if (m_listener != nullptr) {
                m_listener->onPairingFinished(
                    deviceId,
                    ok ? Status{} : std::unexpected(makeError(failure, "fake pairing failure")));
            }
        });
    return {};
}

bool FakeBleApi::isPaired(const BleDeviceId& deviceId) {
    checkThread();
    const FakeDevice* item = device(deviceId);
    return item != nullptr && item->paired;
}

} // namespace hio::ble
