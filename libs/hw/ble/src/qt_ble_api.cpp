#include "qt_ble_api.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/hw/ble/ble_api.h"
#include "hio/hw/ble/types.h"

#include <QBluetoothAddress>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QBluetoothHostInfo>
#include <QBluetoothLocalDevice>
#include <QBluetoothUuid>
#include <QByteArray>
#include <QCoreApplication>
#include <QList>
#include <QLoggingCategory>
#include <QLowEnergyCharacteristic>
#include <QLowEnergyController>
#include <QLowEnergyDescriptor>
#include <QLowEnergyService>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QUuid>
#include <QtCore/qnamespace.h>
#include <QtCore/qtconfigmacros.h>
#include <QtGlobal>
#include <QtLogging>
#include <QtTypes>

#if QT_CONFIG(permissions)
#include <QPermissions>
#endif

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>

namespace hio::ble {

namespace {

std::string enumName(int value) { return std::to_string(value); }

} // namespace

namespace detail {

WriteMode writeModeFor(QLowEnergyCharacteristic::PropertyTypes properties) {
    return properties.testFlag(QLowEnergyCharacteristic::WriteNoResponse)
               ? WriteMode::WithoutResponse
               : WriteMode::WithResponse;
}

// The one mapping of link errors (review B7, ADR-0084): ble.not_paired only when the platform names
// an authentication / encryption problem - Qt reports it as
// QLowEnergyController::AuthorizationError (CCCD or RX write). Qt exposes no ATT error code for
// service errors, so a failed CCCD write without that detail is ble.connect_failed (also while the
// OS pairs) and a failed RX write ble.write_failed. HIL (F1/F2) must confirm per OS which error Qt
// reports for an unbonded link; a passkey never entered ends as ble.not_paired by the 60 s pairing
// deadline (NusLink, Q6) in any case.
Error mapLinkError(const QtLinkError& error) {
    const bool ready = error.phase == LinkPhase::Ready;
    if (error.controller) {
        const std::string detail =
            "QLowEnergyController error " + enumName(static_cast<int>(*error.controller));
        switch (*error.controller) {
        case QLowEnergyController::AuthorizationError:
            return makeError(ErrorCode::BleNotPaired, detail);
        case QLowEnergyController::MissingPermissionsError:
            return makeError(ErrorCode::BlePermissionDenied, detail);
        case QLowEnergyController::InvalidBluetoothAdapterError:
            return makeError(ErrorCode::BleAdapterNotFound, detail);
        case QLowEnergyController::RemoteHostClosedError:
        case QLowEnergyController::ConnectionError:
        case QLowEnergyController::UnknownRemoteDeviceError:
        case QLowEnergyController::NetworkError:
        case QLowEnergyController::UnknownError:
        case QLowEnergyController::AdvertisingError:
        case QLowEnergyController::RssiReadError:
        case QLowEnergyController::NoError:
            break;
        }
        return makeError(ready ? ErrorCode::BleDisconnected : ErrorCode::BleConnectFailed, detail);
    }
    const std::string detail =
        "QLowEnergyService error " +
        enumName(static_cast<int>(error.service.value_or(QLowEnergyService::UnknownError)));
    if (ready) {
        return makeError(ErrorCode::BleWriteFailed, detail);
    }
    Error failed = makeError(ErrorCode::BleConnectFailed, detail);
    if (error.phase == LinkPhase::PairingPending) {
        // HIL H2: a rejected CCCD write is not a weak signal; the device refused the console
        // (still connected from an earlier link or another computer, or the pairing failed).
        failed.cause = QCoreApplication::translate(
                           "hio::ble", "The device refused to start the console: it may still "
                                       "be connected (to this or another computer) or the "
                                       "pairing was not completed.")
                           .toStdString();
        failed.suggestion =
            QCoreApplication::translate("hio::ble",
                                        "Wait a few seconds and try again. If it repeats, "
                                        "disconnect the device in the system Bluetooth settings "
                                        "or restart it.")
                .toStdString();
    }
    return failed;
}

Error pairingFailed(std::string detail) {
    Error error = makeError(ErrorCode::BlePairingFailed, std::move(detail));
#ifdef Q_OS_LINUX
    // HIL H1 rework: with the host's bond removed and no agent, BlueZ connects first and fails
    // after ~24 s, so only the instant failure is known to be "no agent".
    error.cause = QCoreApplication::translate(
                      "hio::ble", "No passkey dialog appeared (no pairing agent is running), or "
                                  "the passkey was wrong, or one side still has an old bond.")
                      .toStdString();
    error.suggestion =
        QCoreApplication::translate(
            "hio::ble",
            "Pair from the system Bluetooth settings, or with bluetoothctl (scan le, pair <id>). "
            "If a dialog appeared, check the passkey, remove the device in the system Bluetooth "
            "settings and pair again.")
            .toStdString();
#endif
    return error;
}

Error pairingFailure(QBluetoothLocalDevice::Error error, std::chrono::milliseconds sinceRequest) {
    const std::string detail = std::format("QBluetoothLocalDevice error {} after {} ms",
                                           static_cast<int>(error), sinceRequest.count());
    if (error == QBluetoothLocalDevice::MissingPermissionsError) {
        return makeError(ErrorCode::BlePermissionDenied, detail);
    }
#ifdef Q_OS_LINUX
    // HIL H1: without a running pairing agent BlueZ cancels the pairing at once (Qt: PairingError,
    // BlueZ org.bluez.Error.AuthenticationCanceled) and no dialog appears. A user who cancels or
    // types a wrong passkey takes longer than noAgentWindow. To be confirmed in HIL: the timing
    // with an agent and a quick cancel, and other desktops (KDE, headless).
    if (error == QBluetoothLocalDevice::PairingError && sinceRequest < noAgentWindow) {
        return makeError(ErrorCode::BleNoPairingAgent, detail);
    }
#endif
    return pairingFailed(detail);
}

// The discovery agent's errors; only the agent is used (legacy null-dereference bug).
Error mapDiscoveryError(QBluetoothDeviceDiscoveryAgent::Error error, const QString& text) {
    const std::string detail = std::format("QBluetoothDeviceDiscoveryAgent error {}: {}",
                                           static_cast<int>(error), text.toStdString());
    switch (error) {
    case QBluetoothDeviceDiscoveryAgent::PoweredOffError:
        return makeError(ErrorCode::BleAdapterOff, detail);
    case QBluetoothDeviceDiscoveryAgent::MissingPermissionsError:
        return makeError(ErrorCode::BlePermissionDenied, detail);
    case QBluetoothDeviceDiscoveryAgent::InvalidBluetoothAdapterError:
    case QBluetoothDeviceDiscoveryAgent::UnsupportedPlatformError:
    case QBluetoothDeviceDiscoveryAgent::UnsupportedDiscoveryMethod:
        return makeError(ErrorCode::BleAdapterNotFound, detail);
    case QBluetoothDeviceDiscoveryAgent::NoError:
    case QBluetoothDeviceDiscoveryAgent::InputOutputError:
    case QBluetoothDeviceDiscoveryAgent::LocationServiceTurnedOffError:
    case QBluetoothDeviceDiscoveryAgent::UnknownError:
        break;
    }
    return makeError(ErrorCode::BleStackUnavailable, detail);
}

} // namespace detail

namespace {

using detail::LinkPhase;
using detail::mapDiscoveryError;
using detail::mapLinkError;
using detail::QtLinkError;

QBluetoothUuid uuidOf(const std::string& text) {
    return QBluetoothUuid{QUuid::fromString(QString::fromStdString(text))};
}

std::string uuidText(const QBluetoothUuid& uuid) {
    return toLowerAscii(uuid.toString(QUuid::WithoutBraces).toStdString());
}

// Q7: an adapter the kernel knows (/sys/class/bluetooth/hci*) that Qt does not see means BlueZ is
// not reachable (Linux only).
bool kernelHasAdapter() {
#ifdef Q_OS_LINUX
    std::error_code ec;
    for (std::filesystem::directory_iterator it("/sys/class/bluetooth", ec), end; !ec && it != end;
         it.increment(ec)) {
        if (it->path().filename().string().starts_with("hci")) {
            return true;
        }
    }
#endif
    return false;
}

QByteArray bytesOf(std::span<const std::byte> chunk) {
    QByteArray bytes;
    bytes.reserve(static_cast<qsizetype>(chunk.size()));
    for (const std::byte b : chunk) {
        bytes.append(static_cast<char>(b));
    }
    return bytes;
}

} // namespace

BleDeviceId deviceIdOf(const QBluetoothDeviceInfo& info) {
    // canonicalDeviceId()'s form (review B6).
    if (!info.address().isNull()) {
        return info.address().toString().toUpper().toStdString();
    }
    return info.deviceUuid().toString(QUuid::WithoutBraces).toStdString(); // macOS
}

QtBleApi::QtBleApi() = default;

QtBleApi::~QtBleApi() {
    m_listener = nullptr;
    for (auto& [id, link] : m_links) {
        release(link);
    }
    m_links.clear();
    if (m_agent) {
        QObject::disconnect(m_agent.get(), nullptr, this, nullptr);
        m_agent->stop();
    }
    // H2: the released controllers are deleted only once unconnected (bounded wait; BleHost::stop).
    m_closer.waitAll();
    // m_agent, m_local and (as children) the released controllers go on this, the BLE, thread.
}

void QtBleApi::setListener(IBleListener* listener) { m_listener = listener; }

QBluetoothLocalDevice& QtBleApi::local() {
    if (!m_local) {
        m_local = std::make_unique<QBluetoothLocalDevice>();
        QObject::connect(
            m_local.get(), &QBluetoothLocalDevice::pairingFinished, this,
            [this](const QBluetoothAddress& address, QBluetoothLocalDevice::Pairing pairing) {
                const BleDeviceId id = address.toString().toUpper().toStdString();
                std::erase_if(m_pairing, [&id](const PendingPairing& pending) {
                    return sameDeviceId(pending.device, id);
                });
                if (m_listener == nullptr) {
                    return;
                }
                m_listener->onPairingFinished(
                    id, pairing == QBluetoothLocalDevice::Unpaired
                            ? std::unexpected(detail::pairingFailed("still unpaired"))
                            : Status{});
            });
        QObject::connect(
            m_local.get(), &QBluetoothLocalDevice::errorOccurred, this,
            [this](QBluetoothLocalDevice::Error error) {
                // The error names no device: every pending request failed.
                const auto pending = std::exchange(m_pairing, {});
                const auto now = std::chrono::steady_clock::now();
                for (const PendingPairing& request : pending) {
                    if (m_listener != nullptr) {
                        m_listener->onPairingFinished(
                            request.device,
                            std::unexpected(detail::pairingFailure(
                                error, std::chrono::duration_cast<std::chrono::milliseconds>(
                                           now - request.requested))));
                    }
                }
            });
    }
    return *m_local;
}

AdapterInfo QtBleApi::adapter() {
    AdapterInfo info;
#if QT_CONFIG(permissions)
    if (QCoreApplication::instance() != nullptr) {
        QBluetoothPermission permission;
        permission.setCommunicationModes(QBluetoothPermission::Access);
        switch (QCoreApplication::instance()->checkPermission(permission)) {
        case Qt::PermissionStatus::Undetermined:
            info.state = AdapterState::PermissionUndetermined;
            return info; // reading the adapter could trigger the macOS prompt (ADR-0088)
        case Qt::PermissionStatus::Denied:
            info.state = AdapterState::PermissionDenied;
            return info;
        case Qt::PermissionStatus::Granted:
            break;
        }
    }
#endif
    const QList<QBluetoothHostInfo> hosts = QBluetoothLocalDevice::allDevices();
    if (hosts.isEmpty()) {
        info.state = kernelHasAdapter() ? AdapterState::StackUnavailable : AdapterState::NotFound;
        return info;
    }
    QBluetoothLocalDevice& device = local();
    if (!device.isValid()) {
        info.state = AdapterState::NotFound;
        return info;
    }
    info.name = device.name().toStdString();
    if (!device.address().isNull()) {
        info.address = device.address().toString().toStdString();
    }
    info.state = device.hostMode() == QBluetoothLocalDevice::HostPoweredOff
                     ? AdapterState::PoweredOff
                     : AdapterState::Ready;
    return info;
}

void QtBleApi::remember(const QBluetoothDeviceInfo& info) {
    const BleDeviceId id = deviceIdOf(info);
    if (!m_known.contains(id)) {
        if (m_known.size() >= maxKnownDevices && !m_knownOrder.empty()) {
            m_known.erase(m_knownOrder.front());
            m_knownOrder.pop_front();
        }
        m_knownOrder.push_back(id);
    }
    m_known.insert_or_assign(id, info);
}

void QtBleApi::advertisement(const QBluetoothDeviceInfo& info) {
    if ((info.coreConfigurations() & QBluetoothDeviceInfo::LowEnergyCoreConfiguration) == 0) {
        return;
    }
    remember(info);
    if (m_listener == nullptr) {
        return;
    }
    Advertisement advertisement{.id = deviceIdOf(info),
                                .name = info.name().toStdString(),
                                .rssi = info.rssi(),
                                .serviceUuids = {},
                                .seen = std::chrono::steady_clock::now()};
    for (const QBluetoothUuid& uuid : info.serviceUuids()) {
        advertisement.serviceUuids.push_back(uuidText(uuid));
    }
    m_listener->onAdvertisement(advertisement);
}

Status QtBleApi::startScan(std::chrono::milliseconds duration) {
    if (auto problem = adapterError(adapter())) {
        return std::unexpected(std::move(*problem));
    }
    if (!m_agent) {
        m_agent = std::make_unique<QBluetoothDeviceDiscoveryAgent>();
        QObject::connect(m_agent.get(), &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this,
                         [this](const QBluetoothDeviceInfo& info) { advertisement(info); });
        QObject::connect(m_agent.get(), &QBluetoothDeviceDiscoveryAgent::deviceUpdated, this,
                         [this](const QBluetoothDeviceInfo& info,
                                QBluetoothDeviceInfo::Fields /*fields*/) { advertisement(info); });
        QObject::connect(m_agent.get(), &QBluetoothDeviceDiscoveryAgent::finished, this, [this] {
            if (m_scanning && m_listener != nullptr) {
                m_scanning = false;
                m_listener->onScanFinished(std::nullopt);
            }
        });
        QObject::connect(m_agent.get(), &QBluetoothDeviceDiscoveryAgent::errorOccurred, this,
                         [this](QBluetoothDeviceDiscoveryAgent::Error error) {
                             // Only the agent itself is used here (legacy null-dereference bug).
                             const Error mapped = mapDiscoveryError(error, m_agent->errorString());
                             if (m_scanning && m_listener != nullptr) {
                                 m_scanning = false;
                                 m_listener->onScanFinished(mapped);
                             }
                         });
    }
    m_agent->setLowEnergyDiscoveryTimeout(static_cast<int>(duration.count()));
    m_scanning = true;
    m_agent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
    return {};
}

void QtBleApi::stopScan() {
    m_scanning = false;
    if (m_agent && m_agent->isActive()) {
        m_agent->stop();
    }
}

QtBleApi::Link* QtBleApi::find(LinkId id) {
    const auto it = m_links.find(id);
    return it == m_links.end() ? nullptr : &it->second;
}

void QtBleApi::release(Link& link) {
    if (!link.service.isNull()) {
        QObject::disconnect(link.service, nullptr, this, nullptr);
    }
    if (!link.controller.isNull()) {
        QLowEnergyController* controller = link.controller;
        QObject::disconnect(controller, nullptr, this, nullptr);
        controller->disconnectFromDevice();
        // H2: deleted (with its service object, a child) only once it is unconnected, at most
        // after ControllerCloser::defaultBound; a new connect to the device waits for that.
        m_closer.close(
            controller, link.device, controller->state() == QLowEnergyController::UnconnectedState,
            [controller](QObject* context, std::function<void()> onGone) {
                return QObject::connect(
                    controller, &QLowEnergyController::stateChanged, context,
                    [onGone = std::move(onGone)](QLowEnergyController::ControllerState state) {
                        if (state == QLowEnergyController::UnconnectedState) {
                            onGone();
                        }
                    });
            });
    }
    link.controller.clear();
    link.service.clear();
}

detail::LinkPhase QtBleApi::phaseOf(const Link& link) {
    if (link.ready) {
        return LinkPhase::Ready;
    }
    return link.cccdPending ? LinkPhase::PairingPending : LinkPhase::Connecting;
}

void QtBleApi::lost(LinkId id, Error error) {
    Link* link = find(id);
    if (link == nullptr) {
        return;
    }
    release(*link);
    m_links.erase(id);
    // Review B5: reported from the event loop, never from inside an IBleApi call (a controller may
    // emit errorOccurred synchronously inside connectToDevice()). N5: until then write() returns
    // this error; disconnect() cancels the report.
    m_pendingLoss.insert_or_assign(id, std::move(error));
    QTimer::singleShot(0, this, [this, id] {
        auto pending = m_pendingLoss.extract(id);
        if (pending && m_listener != nullptr) {
            m_listener->onLinkLost(id, std::move(pending.mapped()));
        }
    });
}

Result<LinkId> QtBleApi::connect(const BleDeviceId& device, const NusUuids& uuids) {
    QBluetoothDeviceInfo info;
    if (const auto known = m_known.find(device); known != m_known.end()) {
        info = known->second;
    } else {
#ifdef Q_OS_MACOS
        info = QBluetoothDeviceInfo(
            QBluetoothUuid(QUuid::fromString(QString::fromStdString(device))), QString(), 0);
#else
        info =
            QBluetoothDeviceInfo(QBluetoothAddress(QString::fromStdString(device)), QString(), 0);
#endif
        info.setCoreConfigurations(QBluetoothDeviceInfo::LowEnergyCoreConfiguration);
    }
    const LinkId id = m_nextLink++;
    m_links.emplace(id, Link{.device = device,
                             .info = info,
                             .uuids = uuids,
                             .controller = nullptr,
                             .service = nullptr,
                             .rx = {},
                             .tx = {},
                             .mode = WriteMode::WithResponse,
                             .cccdPending = false,
                             .ready = false,
                             .writes = {}});
    // H2: after the controller of an earlier link to this device is gone (a reopen right after a
    // close), at once otherwise.
    m_closer.whenClosed(device, [this, id] { startController(id); });
    return id;
}

void QtBleApi::startController(LinkId id) {
    Link* link = find(id);
    if (link == nullptr) {
        return; // disconnected meanwhile
    }
    // Qt parent ownership: a child of this object, released through m_closer (release()).
    QLowEnergyController* controller = QLowEnergyController::createCentral(link->info, this);
    if (controller == nullptr) {
        lost(id, makeError(ErrorCode::BleConnectFailed, "no controller"));
        return;
    }
    link->controller = controller;
    const BleDeviceId device = link->device;
    QObject::connect(controller, &QLowEnergyController::connected, this, [this, id] {
        if (Link* link = find(id); link != nullptr && link->controller) {
            link->controller->discoverServices();
        }
    });
    QObject::connect(controller, &QLowEnergyController::discoveryFinished, this,
                     [this, id] { servicesDiscovered(id); });
    QObject::connect(controller, &QLowEnergyController::errorOccurred, this,
                     [this, id](QLowEnergyController::Error error) {
                         if (Link* link = find(id)) {
                             lost(id, mapLinkError(QtLinkError{.controller = error,
                                                               .service = std::nullopt,
                                                               .phase = phaseOf(*link)}));
                         }
                     });
    QObject::connect(controller, &QLowEnergyController::disconnected, this, [this, id] {
        if (Link* link = find(id)) {
            lost(id,
                 makeError(link->ready ? ErrorCode::BleDisconnected : ErrorCode::BleConnectFailed,
                           "remote disconnect"));
        }
    });
    qCInfo(log::lcBle) << "Connecting to" << toQString(device);
    controller->connectToDevice();
}

void QtBleApi::servicesDiscovered(LinkId id) {
    Link* link = find(id);
    if (link == nullptr || link->controller.isNull()) {
        return;
    }
    const QBluetoothUuid serviceUuid = uuidOf(link->uuids.service);
    if (!link->controller->services().contains(serviceUuid)) {
        lost(id, makeError(ErrorCode::BleServiceNotFound, "NUS service not offered"));
        return;
    }
    // Qt parent ownership: a child of the controller.
    QLowEnergyService* service =
        link->controller->createServiceObject(serviceUuid, link->controller);
    if (service == nullptr) {
        lost(id, makeError(ErrorCode::BleServiceNotFound, "no service object"));
        return;
    }
    link->service = service;
    QObject::connect(service, &QLowEnergyService::stateChanged, this,
                     [this, id](QLowEnergyService::ServiceState state) {
                         if (state == QLowEnergyService::RemoteServiceDiscovered) {
                             detailsDiscovered(id);
                         }
                     });
    QObject::connect(service, &QLowEnergyService::errorOccurred, this,
                     [this, id](QLowEnergyService::ServiceError error) {
                         if (Link* item = find(id)) {
                             lost(id, mapLinkError(QtLinkError{.controller = std::nullopt,
                                                               .service = error,
                                                               .phase = phaseOf(*item)}));
                         }
                     });
    QObject::connect(
        service, &QLowEnergyService::characteristicChanged, this,
        [this, id](const QLowEnergyCharacteristic& characteristic, const QByteArray& value) {
            Link* item = find(id);
            if (item == nullptr || characteristic.uuid() != item->tx.uuid() ||
                m_listener == nullptr) {
                return;
            }
            m_listener->onLinkData(
                id, std::as_bytes(
                        std::span(value.constData(), static_cast<std::size_t>(value.size()))));
        });
    QObject::connect(service, &QLowEnergyService::characteristicWritten, this,
                     [this, id](const QLowEnergyCharacteristic& /*c*/, const QByteArray& /*v*/) {
                         Link* item = find(id);
                         if (item == nullptr || item->writes.empty()) {
                             return;
                         }
                         const std::uint64_t token = item->writes.front();
                         item->writes.pop_front();
                         if (m_listener != nullptr) {
                             m_listener->onWriteDone(id, token, std::nullopt);
                         }
                     });
    QObject::connect(
        service, &QLowEnergyService::descriptorWritten, this,
        [this, id](const QLowEnergyDescriptor& descriptor, const QByteArray& /*v*/) {
            Link* item = find(id);
            if (item == nullptr || !item->cccdPending ||
                descriptor.uuid() !=
                    QBluetoothUuid(
                        QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration)) {
                return;
            }
            item->cccdPending = false;
            item->ready = true;
            // H4 (HIL, BlueZ 5.85): Qt's BlueZ D-Bus backend reports 23 here - the negotiated ATT
            // MTU is only a D-Bus property (GattCharacteristic1.MTU) that Qt does not read, and
            // this project uses no D-Bus of its own. Lines then go in 20-byte chunks (correct,
            // only slower). CoreBluetooth and WinRT should report the real value (to confirm in
            // F2).
            const int mtu = item->controller ? item->controller->mtu() : -1;
            const LinkReady ready{.mtu = std::cmp_greater_equal(mtu, defaultMtu)
                                             ? static_cast<std::uint16_t>(mtu)
                                             : defaultMtu,
                                  .writeMode = item->mode};
            if (m_listener != nullptr) {
                m_listener->onLinkReady(id, ready);
            }
        });
    service->discoverDetails();
}

void QtBleApi::detailsDiscovered(LinkId id) {
    Link* link = find(id);
    if (link == nullptr || link->service.isNull()) {
        return;
    }
    link->rx = link->service->characteristic(uuidOf(link->uuids.rx));
    link->tx = link->service->characteristic(uuidOf(link->uuids.tx));
    if (!link->rx.isValid() || !link->tx.isValid()) {
        lost(id, makeError(ErrorCode::BleServiceNotFound, "NUS characteristics missing"));
        return;
    }
    // A bit test (legacy bug: equality): any RX that allows write-without-response uses it.
    link->mode = detail::writeModeFor(link->rx.properties());
    const QLowEnergyDescriptor cccd = link->tx.descriptor(
        QBluetoothUuid(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration));
    if (!cccd.isValid()) {
        lost(id, makeError(ErrorCode::BleServiceNotFound, "TX has no CCCD"));
        return;
    }
    link->cccdPending = true;
    if (m_listener != nullptr) {
        m_listener->onLinkConnecting(id, true);
    }
    // An unbonded link pairs here: the OS shows its passkey dialog (ADR-0084).
    link->service->writeDescriptor(cccd, QLowEnergyCharacteristic::CCCDEnableNotification);
}

Status QtBleApi::write(LinkId id, std::span<const std::byte> chunk, WriteMode mode,
                       std::uint64_t token) {
    Link* link = find(id);
    if (const auto pending = m_pendingLoss.find(id);
        link == nullptr && pending != m_pendingLoss.end()) {
        return std::unexpected(pending->second); // N5: the real reason, not ble.write_failed
    }
    if (link == nullptr || !link->ready || link->service.isNull()) {
        return std::unexpected(makeError(ErrorCode::BleWriteFailed, "the link is not ready"));
    }
    if (mode == WriteMode::WithResponse) {
        link->writes.push_back(token);
        link->service->writeCharacteristic(link->rx, bytesOf(chunk),
                                           QLowEnergyService::WriteWithResponse);
        return {};
    }
    link->service->writeCharacteristic(link->rx, bytesOf(chunk),
                                       QLowEnergyService::WriteWithoutResponse);
    // No confirmation for write-without-response: handed to the stack. Reported from the event
    // loop, never from inside write().
    QTimer::singleShot(0, this, [this, id, token] {
        if (find(id) != nullptr && m_listener != nullptr) {
            m_listener->onWriteDone(id, token, std::nullopt);
        }
    });
    return {};
}

void QtBleApi::disconnect(LinkId id) {
    m_pendingLoss.erase(id); // no onLinkLost after disconnect()
    if (Link* link = find(id)) {
        release(*link);
        m_links.erase(id);
    }
}

Status QtBleApi::requestPairing(const BleDeviceId& device) {
#ifdef Q_OS_MACOS
    static_cast<void>(device);
    return std::unexpected(makeError(ErrorCode::BlePairingNotSupported, "macOS"));
#else
    if (auto problem = adapterError(adapter())) {
        return std::unexpected(std::move(*problem));
    }
    const QBluetoothAddress address(QString::fromStdString(device));
    if (address.isNull()) {
        return std::unexpected(
            makeError(ErrorCode::UsageInvalidArgument, "not a Bluetooth address: " + device));
    }
    if (std::ranges::none_of(m_pairing, [&device](const PendingPairing& pending) {
            return sameDeviceId(pending.device, device);
        })) {
        m_pairing.push_back(
            PendingPairing{.device = device, .requested = std::chrono::steady_clock::now()});
    }
    local().requestPairing(address, QBluetoothLocalDevice::Paired);
    return {};
#endif
}

bool QtBleApi::isPaired(const BleDeviceId& device) {
#ifdef Q_OS_MACOS
    static_cast<void>(device);
    return false;
#else
    const QBluetoothAddress address(QString::fromStdString(device));
    if (address.isNull() || QBluetoothLocalDevice::allDevices().isEmpty()) {
        return false;
    }
    return local().pairingStatus(address) != QBluetoothLocalDevice::Unpaired;
#endif
}

} // namespace hio::ble
