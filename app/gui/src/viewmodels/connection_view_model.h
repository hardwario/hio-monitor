#pragma once

// ConnectionViewModel - the device bar of a tab (plan/features/0004-m3-gui-shell.md, B4;
// ADR-0052, ADR-0053).
//
// Shows the probe, the target (changeable while no session exists), the connection state as text
// (every ConnectionEvent of the tab's session, plus the tab's busy text) and the primary button
// (Connect / Cancel / Disconnect). The ⋯ Advanced popover edits speed, RTT latency,
// auto-reconnect, reset on open and "Save console to log file"; the values apply on the next
// connect. Defaults come from the settings (jlink/speedKhz, rtt/latencyMs, rtt/autoReconnect,
// rtt/resetOnOpen; 0 = the profile's value) and are remembered per probe serial number
// (probes/<sn>/..., the 32 most recently changed probes, probes/recent).
//
// J-Link speed (ADR-0111): "Automatic speed" (autoSpeed, on by default, ADR-0090) or the fixed
// value speedKhz (kept while automatic; 4000 kHz when none was stored). The speed keys store 0 for
// automatic; 0, a missing key or a value outside minSpeedKhz..maxSpeedKhz means automatic (for
// jlink/speedKhz: the profile's default, automatic for every profile since ADR-0090).
//
// The connection itself is run by the owning DeviceTabViewModel; this object forwards the actions
// and presents its state.
//
// Ownership: child of its DeviceTabViewModel (Qt parent). GUI thread only.

#include "hio/device/session_events.h"
#include "viewmodels/error_info.h"

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QtQmlIntegration>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace hio::device {
struct TargetProfile;
} // namespace hio::device

namespace hio::gui {

class DeviceTabViewModel;
class SettingsStore;

// The J-Link options of one connect (SessionOptions subset).
struct ConnectionOptions {
    static constexpr int defaultFixedSpeedKhz = 4000;

    bool autoSpeed = true;               // ADR-0111
    int speedKhz = defaultFixedSpeedKhz; // the fixed speed, used when !autoSpeed
    int latencyMs = 0;
    bool autoReconnect = true;
    bool resetOnOpen = false;
    bool logToFile = true;

    // SessionOptions::speedKhz: nullopt = automatic.
    [[nodiscard]] std::optional<int> sessionSpeedKhz() const {
        return autoSpeed ? std::nullopt : std::optional<int>(speedKhz);
    }
    // ADR-0110: the warning for a fixed speed below 125 kHz (translated); empty otherwise.
    [[nodiscard]] QString speedWarning() const;
    // ADR-0111: a stored speed (jlink/speedKhz, probes/<sn>/speedKhz): a value in
    // minSpeedKhz..maxSpeedKhz is fixed; 0, a missing key or any other value means automatic.
    [[nodiscard]] static std::optional<int> storedSpeed(const QVariant& value);
    // Applies a stored speed: automatic keeps the last fixed value (speedKhz).
    void applyStoredSpeed(std::optional<int> speed);
    // The value to store for the speed: 0 = automatic.
    [[nodiscard]] int speedToStore() const { return autoSpeed ? 0 : speedKhz; }
};

class ConnectionViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by DeviceTabViewModel")

    Q_PROPERTY(QString probeLabel READ probeLabel NOTIFY probeChanged)
    Q_PROPERTY(QString serial READ serial NOTIFY probeChanged)
    Q_PROPERTY(QString targetId READ targetId WRITE setTargetId NOTIFY targetChanged)
    Q_PROPERTY(QString targetTitle READ targetTitle NOTIFY targetChanged)
    // [{id, label (= id, 0021 ADR-0168), title}]
    Q_PROPERTY(QVariantList targets READ targets NOTIFY targetChanged)
    Q_PROPERTY(bool canChangeTarget READ canChangeTarget NOTIFY stateChanged)
    // "idle", "connecting", "connected", "reconnecting", "error", "busy"
    Q_PROPERTY(QString stateKind READ stateKind NOTIFY stateChanged)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)
    // "connect", "cancel", "disconnect" (the device bar's primary button)
    Q_PROPERTY(QString action READ action NOTIFY stateChanged)
    Q_PROPERTY(bool actionEnabled READ actionEnabled NOTIFY stateChanged)
    Q_PROPERTY(double targetVolts READ targetVolts NOTIFY stateChanged)  // NaN = unknown
    Q_PROPERTY(hio::gui::ErrorInfo error READ error NOTIFY errorChanged) // invalid when none
    Q_PROPERTY(hio::gui::ErrorInfo warning READ warning NOTIFY warningChanged)
    Q_PROPERTY(bool canChooseAnotherProbe READ canChooseAnotherProbe NOTIFY errorChanged)

    Q_PROPERTY(bool autoSpeed READ autoSpeed WRITE setAutoSpeed NOTIFY optionsChanged)
    Q_PROPERTY(int speedKhz READ speedKhz WRITE setSpeedKhz NOTIFY optionsChanged)
    // ADR-0110: the warning for a fixed speed below 125 kHz (translated); empty otherwise.
    Q_PROPERTY(QString speedWarning READ speedWarning NOTIFY optionsChanged)
    Q_PROPERTY(int latencyMs READ latencyMs WRITE setLatencyMs NOTIFY optionsChanged)
    Q_PROPERTY(bool autoReconnect READ autoReconnect WRITE setAutoReconnect NOTIFY optionsChanged)
    Q_PROPERTY(bool resetOnOpen READ resetOnOpen WRITE setResetOnOpen NOTIFY optionsChanged)
    Q_PROPERTY(bool logToFile READ logToFile WRITE setLogToFile NOTIFY optionsChanged)
    Q_PROPERTY(int minSpeedKhz READ minSpeedKhz CONSTANT)
    Q_PROPERTY(int maxSpeedKhz READ maxSpeedKhz CONSTANT)
    Q_PROPERTY(int minLatencyMs READ minLatencyMs CONSTANT)
    Q_PROPERTY(int maxLatencyMs READ maxLatencyMs CONSTANT)

public:
    static constexpr std::size_t maxRememberedProbes = 32;

    ConnectionViewModel(DeviceTabViewModel* tab, SettingsStore* settings);

    [[nodiscard]] QString probeLabel() const;
    [[nodiscard]] QString serial() const;
    [[nodiscard]] QString targetId() const;
    void setTargetId(const QString& id);
    [[nodiscard]] QString targetTitle() const;
    [[nodiscard]] QVariantList targets() const;
    [[nodiscard]] bool canChangeTarget() const;
    [[nodiscard]] QString stateKind() const;
    [[nodiscard]] QString stateText() const;
    [[nodiscard]] QString action() const;
    [[nodiscard]] bool actionEnabled() const;
    [[nodiscard]] double targetVolts() const;
    [[nodiscard]] ErrorInfo error() const { return m_errorInfo; }
    [[nodiscard]] ErrorInfo warning() const { return m_warning; }
    [[nodiscard]] bool canChooseAnotherProbe() const;

    [[nodiscard]] bool autoSpeed() const noexcept { return m_options.autoSpeed; }
    void setAutoSpeed(bool value);
    [[nodiscard]] int speedKhz() const noexcept { return m_options.speedKhz; }
    void setSpeedKhz(int value);
    [[nodiscard]] QString speedWarning() const;
    [[nodiscard]] int latencyMs() const noexcept { return m_options.latencyMs; }
    void setLatencyMs(int value);
    [[nodiscard]] bool autoReconnect() const noexcept { return m_options.autoReconnect; }
    void setAutoReconnect(bool value);
    [[nodiscard]] bool resetOnOpen() const noexcept { return m_options.resetOnOpen; }
    void setResetOnOpen(bool value);
    [[nodiscard]] bool logToFile() const noexcept { return m_options.logToFile; }
    void setLogToFile(bool value);
    [[nodiscard]] static int minSpeedKhz();
    [[nodiscard]] static int maxSpeedKhz();
    [[nodiscard]] static int minLatencyMs();
    [[nodiscard]] static int maxLatencyMs();

    Q_INVOKABLE void connectDevice();
    Q_INVOKABLE void disconnectDevice(); // also Cancel while connecting
    Q_INVOKABLE void reconnect();
    Q_INVOKABLE void toggleConnection(); // Ctrl+Shift+D
    Q_INVOKABLE void triggerAction();    // the primary button
    // The ErrorPanel's primary action (ErrorAction): Reconnect, Retry -> reconnect; Dismiss,
    // ChooseFile -> dismissError. OpenSettings is handled by the shell (AppViewModel).
    Q_INVOKABLE void triggerErrorAction(int action);
    Q_INVOKABLE void dismissError();
    Q_INVOKABLE void dismissWarning();
    Q_INVOKABLE void chooseAnotherProbe();

    // ---- driven by the DeviceTabViewModel ----
    [[nodiscard]] const ConnectionOptions& options() const noexcept { return m_options; }
    // Loads the options of the probe (remembered or the defaults of `target`).
    void loadOptions(std::optional<std::uint32_t> serial, const device::TargetProfile& target);
    // Marks the probe as recently used (LRU) when its options are remembered.
    void touchRememberedProbe();
    void setProduct(const QString& product);
    // The state of the tab's session (nullopt = no session).
    void setConnection(std::optional<device::ConnectionEvent> event);
    // An error without a session (open() failed); also cleared by setConnection().
    void setOpenError(std::optional<Error> error);
    void setWarning(const Error& warning);
    // Re-evaluates the texts (busy state, probe, language, speed warning).
    void refresh();

    // The short cause for state texts and markers ("target not powered"), translated.
    [[nodiscard]] static QString shortCause(const Error& error);

signals:
    void probeChanged();
    void targetChanged();
    void stateChanged();
    void errorChanged();
    void warningChanged();
    void optionsChanged();

private:
    void rememberOptions();
    void setError(std::optional<Error> error);

    DeviceTabViewModel* m_tab; // the parent
    SettingsStore* m_settings; // non-owning, owned by AppViewModel (outlives the tabs)
    ConnectionOptions m_options;
    std::optional<std::uint32_t> m_serial;
    bool m_remembered = false; // the probe has stored options
    QString m_product;
    std::optional<device::ConnectionEvent> m_connection;
    std::optional<Error> m_error;
    ErrorInfo m_errorInfo;
    ErrorInfo m_warning;
};

} // namespace hio::gui
