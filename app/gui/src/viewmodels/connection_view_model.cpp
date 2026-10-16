#include "viewmodels/connection_view_model.h"

#include "gui_log.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/device/device_session.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"
#include "settings_store.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"
#include "viewmodels/probe_picker_view_model.h"

#include <QChar>
#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtContainerFwd>
#include <QtCore/qnamespace.h>
#include <QtTypes>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace hio::gui {

namespace {

const QString recentKey = QStringLiteral("probes/recent");

QString probeKey(std::uint32_t serial, const char* name) {
    return QStringLiteral("probes/%1/%2").arg(serial).arg(QString::fromLatin1(name));
}

QStringList recentProbes(const SettingsStore* settings) {
    if (settings == nullptr) {
        return {};
    }
    return settings->value(recentKey).toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
}

// A stored speed (ADR-0111): a value in minSpeedKhz..maxSpeedKhz is fixed; 0, a missing key or any
// other value means automatic (nullopt).
std::optional<int> storedSpeed(const QVariant& value) {
    bool ok = false;
    const int speed = value.toInt(&ok);
    if (!ok || speed < device::minSpeedKhz || speed > device::maxSpeedKhz) {
        return std::nullopt;
    }
    return speed;
}

void applySpeed(ConnectionOptions& options, std::optional<int> speed) {
    options.autoSpeed = !speed.has_value();
    if (speed) {
        options.speedKhz = *speed;
    }
}

void logFailure(const char* what, const Status& status) {
    if (!status) {
        qCWarning(lcGui) << "Could not store" << what << toQString(status.error().message)
                         << toQString(status.error().detail);
    }
}

} // namespace

ConnectionViewModel::ConnectionViewModel(DeviceTabViewModel* tab, SettingsStore* settings)
    : QObject(tab), m_tab(tab), m_settings(settings) {}

QString ConnectionViewModel::probeLabel() const {
    if (!m_serial) {
        return {};
    }
    const QString product = m_product.isEmpty() ? QStringLiteral("J-Link") : m_product;
    return QStringLiteral("%1 · %2").arg(product).arg(*m_serial);
}

QString ConnectionViewModel::serial() const {
    return m_serial ? QString::number(*m_serial) : QString();
}

QString ConnectionViewModel::targetId() const { return m_tab->consoleTargetId(); }

void ConnectionViewModel::setTargetId(const QString& id) {
    if (!canChangeTarget()) {
        targetChanged(); // signal: QML reverts its control
        return;
    }
    m_tab->setConsoleTarget(id);
    targetChanged(); // signal
}

QString ConnectionViewModel::targetTitle() const {
    return ProbePickerViewModel::targetTitle(targetId());
}

QVariantList ConnectionViewModel::targets() const {
    QVariantList list;
    for (const device::TargetProfile& target : m_tab->profile().targets) {
        const QString id = toQString(target.id);
        list.append(QVariantMap{{QStringLiteral("id"), id},
                                {QStringLiteral("title"), ProbePickerViewModel::targetTitle(id)}});
    }
    return list;
}

bool ConnectionViewModel::canChangeTarget() const {
    // A stopped session (fatal error) counts as none: it is replaced on the next connect (S1).
    return (!m_connection || (m_connection->state == device::ConnectionState::Stopped &&
                              m_connection->error.has_value())) &&
           !m_tab->isBusy();
}

QString ConnectionViewModel::stateKind() const {
    if (m_tab->isBusy()) {
        return QStringLiteral("busy");
    }
    if (!m_connection) {
        return m_error ? QStringLiteral("error") : QStringLiteral("idle");
    }
    switch (m_connection->state) {
    case device::ConnectionState::Connecting:
        return QStringLiteral("connecting");
    case device::ConnectionState::Connected:
        return QStringLiteral("connected");
    case device::ConnectionState::Disconnected:
        return QStringLiteral("reconnecting");
    case device::ConnectionState::Stopped:
        // The error can be dismissed (S10).
        return m_error ? QStringLiteral("error") : QStringLiteral("idle");
    }
    return QStringLiteral("idle");
}

QString ConnectionViewModel::stateText() const {
    if (m_tab->isBusy()) {
        return m_tab->busyText();
    }
    if (!m_connection) {
        return m_error ? tr("Disconnected — %1").arg(shortCause(*m_error)) : tr("Disconnected");
    }
    switch (m_connection->state) {
    case device::ConnectionState::Connecting:
        return tr("Connecting…");
    case device::ConnectionState::Connected: {
        const double volts = targetVolts();
        return std::isnan(volts) ? tr("Connected")
                                 : tr("Connected · %1 V").arg(QLocale().toString(volts, 'f', 2));
    }
    case device::ConnectionState::Disconnected:
        return m_connection->error ? tr("Reconnecting… (%1)").arg(shortCause(*m_connection->error))
                                   : tr("Reconnecting…");
    case device::ConnectionState::Stopped:
        return m_error ? tr("Disconnected — %1").arg(shortCause(*m_error)) : tr("Disconnected");
    }
    return tr("Disconnected");
}

QString ConnectionViewModel::action() const {
    if (!m_connection || m_connection->state == device::ConnectionState::Stopped) {
        return QStringLiteral("connect");
    }
    if (m_connection->state == device::ConnectionState::Connecting) {
        return QStringLiteral("cancel");
    }
    return QStringLiteral("disconnect");
}

bool ConnectionViewModel::actionEnabled() const { return !m_tab->isBusy() && m_serial; }

double ConnectionViewModel::targetVolts() const {
    // ADR-0091: the voltage of the last connection event; shown only while connected.
    if (!m_connection || m_connection->state != device::ConnectionState::Connected ||
        !m_connection->targetMillivolts) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return *m_connection->targetMillivolts / 1000.0;
}

bool ConnectionViewModel::canChooseAnotherProbe() const {
    return m_error && m_error->code == ErrorCode::ProbeNotFound;
}

int ConnectionViewModel::minSpeedKhz() { return device::minSpeedKhz; }
int ConnectionViewModel::maxSpeedKhz() { return device::maxSpeedKhz; }
int ConnectionViewModel::minLatencyMs() { return device::minLatencyMs; }
int ConnectionViewModel::maxLatencyMs() { return device::maxLatencyMs; }

void ConnectionViewModel::setAutoSpeed(bool value) {
    if (value == m_options.autoSpeed) {
        return;
    }
    m_options.autoSpeed = value;
    rememberOptions();
    optionsChanged(); // signal
}

QString ConnectionViewModel::speedWarning() const {
    const std::optional<std::string> warning =
        device::slowSpeedWarning(m_options.sessionSpeedKhz());
    return warning ? toQString(*warning) : QString();
}

void ConnectionViewModel::setSpeedKhz(int value) {
    if (value == m_options.speedKhz || value < device::minSpeedKhz || value > device::maxSpeedKhz) {
        optionsChanged(); // signal: QML shows the stored value again
        return;
    }
    m_options.speedKhz = value;
    rememberOptions();
    optionsChanged(); // signal
}

void ConnectionViewModel::setLatencyMs(int value) {
    if (value == m_options.latencyMs || value < device::minLatencyMs ||
        value > device::maxLatencyMs) {
        optionsChanged(); // signal
        return;
    }
    m_options.latencyMs = value;
    rememberOptions();
    optionsChanged(); // signal
}

void ConnectionViewModel::setAutoReconnect(bool value) {
    if (value == m_options.autoReconnect) {
        return;
    }
    m_options.autoReconnect = value;
    rememberOptions();
    optionsChanged(); // signal
}

void ConnectionViewModel::setResetOnOpen(bool value) {
    if (value == m_options.resetOnOpen) {
        return;
    }
    m_options.resetOnOpen = value;
    rememberOptions();
    optionsChanged(); // signal
}

void ConnectionViewModel::setLogToFile(bool value) {
    if (value == m_options.logToFile) {
        return;
    }
    // Per tab only: the default is the console/logToFile setting (plan 0005).
    m_options.logToFile = value;
    optionsChanged(); // signal
}

void ConnectionViewModel::connectDevice() { m_tab->connectDevice(); }

void ConnectionViewModel::disconnectDevice() { m_tab->disconnectDevice(); }

void ConnectionViewModel::reconnect() { m_tab->reconnect(); }

void ConnectionViewModel::toggleConnection() {
    if (action() == QStringLiteral("connect")) {
        connectDevice();
    } else {
        disconnectDevice();
    }
}

void ConnectionViewModel::triggerAction() { toggleConnection(); }

void ConnectionViewModel::triggerErrorAction(int action) {
    switch (action) {
    case error_action::Reconnect:
    case error_action::Retry:
        reconnect();
        break;
    case error_action::Dismiss:
    case error_action::ChooseFile:
        dismissError();
        break;
    default:
        break;
    }
}

void ConnectionViewModel::dismissError() {
    if (!m_error) {
        return;
    }
    m_error.reset();
    m_errorInfo = ErrorInfo();
    errorChanged(); // signal
    stateChanged(); // signal
}

void ConnectionViewModel::dismissWarning() {
    if (!m_warning.valid()) {
        return;
    }
    m_warning = ErrorInfo();
    warningChanged(); // signal
}

void ConnectionViewModel::chooseAnotherProbe() { m_tab->chooseAnotherProbe(); }

void ConnectionViewModel::loadOptions(std::optional<std::uint32_t> serial,
                                      const device::TargetProfile& target) {
    if (serial != m_serial) {
        m_serial = serial;
        probeChanged(); // signal
    }
    ConnectionOptions options;
    applySpeed(options, target.defaultSpeedKhz);
    options.latencyMs = target.defaultLatencyMs;
    if (m_settings != nullptr) {
        // ADR-0111: 0 / missing / out of range = the profile's default (automatic).
        if (const std::optional<int> speed =
                storedSpeed(m_settings->value(QStringLiteral("jlink/speedKhz")))) {
            applySpeed(options, speed);
        }
        if (const int latency = m_settings->value(QStringLiteral("rtt/latencyMs")).toInt();
            latency != 0) {
            options.latencyMs = latency;
        }
        options.autoReconnect = m_settings->value(QStringLiteral("rtt/autoReconnect")).toBool();
        options.resetOnOpen = m_settings->value(QStringLiteral("rtt/resetOnOpen")).toBool();
        // console/logToFile is registered by the console page (0005); default on.
        const QString logKey = QStringLiteral("console/logToFile");
        options.logToFile = !m_settings->isRegistered(logKey) || m_settings->value(logKey).toBool();

        m_remembered = m_serial && recentProbes(m_settings).contains(QString::number(*m_serial));
        if (m_remembered) {
            // ADR-0111: 0 (stored for automatic), missing or out of range = automatic.
            applySpeed(options, storedSpeed(m_settings->value(probeKey(*m_serial, "speedKhz"))));
            options.latencyMs = m_settings->value(probeKey(*m_serial, "latencyMs")).toInt();
            options.autoReconnect =
                m_settings->value(probeKey(*m_serial, "autoReconnect")).toBool();
            options.resetOnOpen = m_settings->value(probeKey(*m_serial, "resetOnOpen")).toBool();
            if (options.latencyMs == 0) {
                options.latencyMs = target.defaultLatencyMs;
            }
        }
    } else {
        m_remembered = false;
    }
    m_options = options;
    optionsChanged(); // signal
    targetChanged();  // signal
}

void ConnectionViewModel::rememberOptions() {
    if (!m_serial || m_settings == nullptr || m_settings->readOnly()) {
        return;
    }
    const std::uint32_t serial = *m_serial;
    // ADR-0111: 0 = automatic.
    logFailure("speedKhz", m_settings->setValue(probeKey(serial, "speedKhz"),
                                                m_options.autoSpeed ? 0 : m_options.speedKhz));
    logFailure("latencyMs",
               m_settings->setValue(probeKey(serial, "latencyMs"), m_options.latencyMs));
    logFailure("autoReconnect",
               m_settings->setValue(probeKey(serial, "autoReconnect"), m_options.autoReconnect));
    logFailure("resetOnOpen",
               m_settings->setValue(probeKey(serial, "resetOnOpen"), m_options.resetOnOpen));
    m_remembered = true;
    touchRememberedProbe();
}

void ConnectionViewModel::touchRememberedProbe() {
    if (!m_remembered || !m_serial || m_settings == nullptr || m_settings->readOnly()) {
        return;
    }
    // Most recently used first, at most maxRememberedProbes; the probes/<sn> group of a dropped
    // probe is removed (ADR-0092).
    const QString serial = QString::number(*m_serial);
    QStringList recent = recentProbes(m_settings);
    recent.removeAll(serial);
    recent.prepend(serial);
    while (recent.size() > static_cast<qsizetype>(maxRememberedProbes)) {
        logFailure("an evicted probe",
                   m_settings->removeGroup(QStringLiteral("probes/") + recent.takeLast()));
    }
    logFailure("probes/recent", m_settings->setValue(recentKey, recent.join(QLatin1Char(','))));
}

void ConnectionViewModel::setProduct(const QString& product) {
    if (product == m_product) {
        return;
    }
    m_product = product;
    probeChanged(); // signal
}

void ConnectionViewModel::setConnection(std::optional<device::ConnectionEvent> event) {
    m_connection = std::move(event);
    if (m_connection) {
        // Connected clears the error; Disconnected / Stopped show theirs.
        const bool connected = m_connection->state == device::ConnectionState::Connected;
        setError(connected ? std::nullopt : m_connection->error);
    }
    if (m_tab->session() != nullptr && m_product.isEmpty()) {
        setProduct(toQString(m_tab->session()->probe().product));
    }
    stateChanged();  // signal
    targetChanged(); // signal: canChangeTarget
}

void ConnectionViewModel::setOpenError(std::optional<Error> error) {
    if (!error && m_connection) {
        return; // the session's error stays
    }
    setError(std::move(error));
    stateChanged(); // signal
}

void ConnectionViewModel::setError(std::optional<Error> error) {
    const bool changed = error.has_value() != m_error.has_value() ||
                         (error && m_error && ErrorInfo(*error) != m_errorInfo);
    m_error = std::move(error);
    m_errorInfo = m_error ? ErrorInfo(*m_error) : ErrorInfo();
    if (changed) {
        errorChanged(); // signal
    }
}

void ConnectionViewModel::setWarning(const Error& warning) {
    m_warning = ErrorInfo(warning);
    warningChanged(); // signal
}

void ConnectionViewModel::refresh() {
    probeChanged();   // signal
    optionsChanged(); // signal: speedWarning (language)
    targetChanged();  // signal
    stateChanged();   // signal
}

QString ConnectionViewModel::shortCause(const Error& error) {
    switch (error.code) {
    case ErrorCode::TargetNoPower:
        return tr("target not powered");
    case ErrorCode::ProbeNotFound:
        return tr("J-Link not found");
    case ErrorCode::ProbeInUseByOtherProcess:
        return tr("J-Link used by another program");
    case ErrorCode::ProbeOpenFailed:
        return tr("J-Link could not be opened");
    case ErrorCode::TargetConnectFailed:
        return tr("target does not respond");
    case ErrorCode::TargetRttNotFound:
    case ErrorCode::TargetRttBufferNotFound:
        return tr("no RTT console found");
    case ErrorCode::TargetRttReadFailed:
    case ErrorCode::TargetRttWriteFailed:
    case ErrorCode::TargetRttWriteTimeout:
        return tr("connection lost");
    case ErrorCode::DependencyJLinkNotFound:
    case ErrorCode::DependencyJLinkLoadFailed:
    case ErrorCode::DependencyJLinkTooOld:
        return tr("J-Link Software missing");
    default:
        return toQString(error.message);
    }
}

} // namespace hio::gui
