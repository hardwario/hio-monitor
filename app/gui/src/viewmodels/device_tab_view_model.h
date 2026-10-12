#pragma once

// DeviceTabViewModel - one device tab (plan/features/0004-m3-gui-shell.md, B2 and § Page-extension
// contract; ADR-0043, ADR-0053, ADR-0059, ADR-0061, ADR-0063).
//
// A tab starts in ChoosingDevice (the probe picker) or, when restored, Disconnected with its
// profile, target and probe serial number. It owns at most one DeviceSession (opened on Connect or
// by a page through ensureSession(); never at start) and one ConsoleLog for all its sessions
// (ADR-0063), and the view models of the per-tab pages of the PageRegistry (Qt parent ownership).
// Pages never own or close the session. A page may keep a pointer only as QPointer, or re-read
// session() on sessionChanged, which is emitted whenever the session is replaced or released,
// also by shutdown() (tab close, quit) before the session is deleted (review S3). Disconnect
// closes the session (the worker is joined, the probe lock and the registry slot are released at
// once) and the QObject is deleted on the next event-loop turn; the log stays.
//
// Busy: one page at a time (setBusy / clearBusy). The tab state is Busy, the rail shows the
// page's badge, the device bar shows the busy text, and closing the tab or quitting asks with the
// page's confirmation text; [Cancel and close] calls the page's cancel callback and the tab closes
// when the page calls clearBusy() (closeReady).
//
// The shell (this class) owns all console markers (ADR-0063, review S5): "— connected —",
// "— disconnected —", "— disconnected: <cause> —" on connection changes, "— paused: <busy text> —"
// on setBusy and "— resumed —" when the busy page clears; pages never add these markers.
//
// Ownership: child of DeviceTabListModel (Qt parent). GUI thread only.

#include "hio/core/result.h"
#include "hio/device/session_events.h"
#include "viewmodels/connection_view_model.h"
#include "viewmodels/probe_picker_view_model.h"

#include <QObject>
#include <QString>
#include <QtCore/qobjectdefs.h>
#include <QtQmlIntegration>
#include <QtTypes>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace hio::jlink {
struct LocatorOptions;
} // namespace hio::jlink

namespace hio::device {
class ConsoleLog;
class DeviceSession;
class SessionRegistry;
struct DeviceProfile;
} // namespace hio::device

namespace hio::gui {

class PageRegistry;
class ProbeScanner;
class SettingsStore;

// What a tab needs from the application (AppViewModel; tests build their own).
struct TabContext {
    device::SessionRegistry* registry = nullptr; // outlives the tabs
    SettingsStore* settings = nullptr;           // outlives the tabs
    const PageRegistry* pages = nullptr;         // may be null (no pages)
    ProbeScanner* scanner = nullptr;             // may be null (no probe list)
    // The locator options for opening a session (environment, setting, search paths).
    std::function<jlink::LocatorOptions()> locatorOptions;
    // The 0-based index of another tab that uses this probe, -1 = none.
    std::function<int(std::uint32_t serial, const DeviceTabViewModel* self)> tabUsingProbe;
    // Shows a toast (AppViewModel, ≤ 3 visible); may be empty.
    std::function<void(QString)> showToast;
};

class DeviceTabViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by DeviceTabListModel")

    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool hasError READ hasError NOTIFY stateChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(hio::gui::ProbePickerViewModel* picker READ picker CONSTANT)
    Q_PROPERTY(hio::gui::ConnectionViewModel* connection READ connection CONSTANT)
    Q_PROPERTY(QObjectList pageViewModels READ pageViewModels CONSTANT) // in PageRegistry order
    Q_PROPERTY(QString currentPage READ currentPage WRITE setCurrentPage NOTIFY currentPageChanged)
    Q_PROPERTY(QString busyPage READ busyPage NOTIFY busyChanged) // "" when idle
    Q_PROPERTY(QString busyText READ busyText NOTIFY busyChanged)

public:
    // Unscoped: QML compares DeviceTabViewModel.Connected etc. with the int property.
    enum State : quint8 { // NOLINT(cppcoreguidelines-use-enum-class)
        ChoosingDevice,
        Disconnected,
        Connecting,
        Connected,
        Busy
    };
    Q_ENUM(State)

    // A new tab in ChoosingDevice.
    DeviceTabViewModel(TabContext context, QObject* parent);
    ~DeviceTabViewModel() override; // closes the session
    DeviceTabViewModel(const DeviceTabViewModel&) = delete;
    DeviceTabViewModel& operator=(const DeviceTabViewModel&) = delete;
    DeviceTabViewModel(DeviceTabViewModel&&) = delete;
    DeviceTabViewModel& operator=(DeviceTabViewModel&&) = delete;

    [[nodiscard]] State state() const;
    [[nodiscard]] bool hasError() const;
    [[nodiscard]] QString title() const;
    [[nodiscard]] ProbePickerViewModel* picker() const { return m_picker; }
    [[nodiscard]] ConnectionViewModel* connection() const { return m_connection; }
    [[nodiscard]] QObjectList pageViewModels() const { return m_pageViewModels; }
    [[nodiscard]] QString currentPage() const { return m_currentPage; }
    void setCurrentPage(const QString& pageId);
    [[nodiscard]] QString busyPage() const;
    [[nodiscard]] QString busyText() const;
    // The view model of a per-tab page (nullptr when unknown or without a view model).
    Q_INVOKABLE [[nodiscard]] QObject* pageViewModel(const QString& pageId) const;

    // --- contract for pages (C++ only) ---
    // nullptr while choosing / disconnected.
    [[nodiscard]] device::DeviceSession* session() const;
    // Opens the tab's session when none exists: always on the console target, with RTT when the
    // target has an RTT console. Operations carry their own target (ADR-0057), so a page never
    // opens a session on another target (review S2). No-op when a working session exists; a
    // stopped one (fatal error) is replaced. Errors: those of DeviceSession::open,
    // dependency.jlink_not_found, usage.invalid_argument (no probe chosen), internal.busy.
    [[nodiscard]] Status ensureSession();
    [[nodiscard]] const device::DeviceProfile& profile() const;
    [[nodiscard]] QString consoleTargetId() const;
    // One log for all sessions of this tab (ADR-0063); passed as SessionOptions::consoleLog.
    [[nodiscard]] const std::shared_ptr<device::ConsoleLog>& consoleLog() const;
    // Busy: one page at a time; a second page's setBusy is ignored (logged). `cancel` is called
    // on [Cancel and close] / quit.
    void setBusy(const QString& pageId, QString busyText, QString confirmText,
                 std::function<void()> cancel);
    void clearBusy(const QString& pageId);
    void showToast(QString text); // queued in AppViewModel (≤ 3 visible)

    // --- shell (AppViewModel, ConnectionViewModel) ---
    // Leaves ChoosingDevice with this configuration (restore or the picker); never connects.
    // Returns false when the profile or target is unknown.
    bool configure(const QString& profileId, const QString& targetId, std::uint32_t serial,
                   const QString& product = {});
    [[nodiscard]] bool choosing() const noexcept { return m_choosing; }
    [[nodiscard]] QString profileId() const;
    [[nodiscard]] std::optional<std::uint32_t> probeSerial() const noexcept { return m_serial; }
    [[nodiscard]] QString product() const { return m_product; }
    // Changes the console target (only without a session).
    void setConsoleTarget(const QString& targetId);
    // Device bar actions.
    void connectDevice();
    void disconnectDevice();
    void reconnect();
    // Back to the picker (closes the session, keeps the log).
    void chooseAnotherProbe();
    [[nodiscard]] bool isBusy() const noexcept { return m_busy.has_value(); }
    [[nodiscard]] QString busyConfirmText() const;
    // [Cancel and close] / quit: calls the busy page's cancel callback (once); with `close`, the
    // tab emits closeReady when the page clears its busy state.
    void cancelBusy(bool close);
    [[nodiscard]] bool closePending() const noexcept { return m_closePending; }
    // Closes the session (tab close, quit).
    void shutdown();
    void retranslate();

signals:
    void stateChanged();
    void titleChanged();
    void currentPageChanged();
    void busyChanged();
    // session() changed (new, replaced, released, shutdown): pages reconnect their handlers and
    // drop their pointers to the old session, which is deleted after this signal.
    void sessionChanged();
    void configurationChanged(); // profile, target or probe changed (ui/tabs is saved)
    void closeReady();           // the busy page finished after cancelBusy(true)
    void tabActivationRequested(int tabIndex);

private:
    struct BusyState {
        QString pageId;
        QString text;
        QString confirmText;
        std::function<void()> cancel;
        bool cancelled = false;
    };

    [[nodiscard]] Status openSession();
    // The session stopped with an error (fatal); see sessionFailed() in the .cpp (S18).
    [[nodiscard]] bool sessionFailed() const;
    void releaseSession();
    void onConnectionChanged(const device::ConnectionEvent& event);
    void addMarker(const QString& text);
    void updateState();

    TabContext m_context;
    ProbePickerViewModel* m_picker = nullptr;         // child
    ConnectionViewModel* m_connection = nullptr;      // child
    QObjectList m_pageViewModels;                     // children
    QStringList m_pageIds;                            // parallel to m_pageViewModels
    const device::DeviceProfile* m_profile = nullptr; // compiled-in, static lifetime
    QString m_targetId;
    std::optional<std::uint32_t> m_serial;
    QString m_product;
    bool m_choosing = true;
    QString m_currentPage;
    std::shared_ptr<device::ConsoleLog> m_consoleLog;
    std::unique_ptr<device::DeviceSession> m_session;
    // Closed sessions, deleted on the next event-loop turn (a session is never deleted inside one
    // of its own signals).
    std::vector<std::unique_ptr<device::DeviceSession>> m_retiredSessions;
    std::optional<BusyState> m_busy;
    bool m_closePending = false;
    State m_state = ChoosingDevice;
    bool m_hasError = false;
};

} // namespace hio::gui
