#include "viewmodels/device_tab_view_model.h"

#include "gui_log.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/device/console_line.h"
#include "hio/device/console_log.h"
#include "hio/device/device_session.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"
#include "hio/hw/jlink/library_locator.h"
#include "viewmodels/connection_view_model.h"
#include "viewmodels/page_registry.h"
#include "viewmodels/probe_picker_view_model.h"

#include <QLoggingCategory>
#include <QMetaObject>
#include <QObject>
#include <QString>
#include <QtCore/qnamespace.h>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace hio::gui {

DeviceTabViewModel::DeviceTabViewModel(TabContext context, QObject* parent)
    : QObject(parent), m_context(std::move(context)),
      m_consoleLog(std::make_shared<device::ConsoleLog>()) {
    const auto profiles = device::ProfileRegistry::all();
    if (!profiles.empty()) {
        m_profile = profiles.front();
        if (!m_profile->targets.empty()) {
            m_targetId = toQString(m_profile->targets.front().id);
        }
    }

    // Qt parent ownership: the picker and the connection view model are children of this tab.
    m_picker = new ProbePickerViewModel( // NOLINT(cppcoreguidelines-owning-memory)
        m_context.scanner,
        [this](std::uint32_t serial) {
            return m_context.tabUsingProbe ? m_context.tabUsingProbe(serial, this) : -1;
        },
        this);
    connect(m_picker, &ProbePickerViewModel::tabActivationRequested, this,
            &DeviceTabViewModel::tabActivationRequested);
    connect(m_picker, &ProbePickerViewModel::connectRequested, this,
            [this](std::uint32_t serial, const QString& product, const QString& profileId,
                   const QString& targetId) {
                if (configure(profileId, targetId, serial, product)) {
                    connectDevice();
                }
            });
    m_connection = new ConnectionViewModel( // NOLINT(cppcoreguidelines-owning-memory)
        this, m_context.settings);
    // A dismissed error clears the tab's red dot too (S10).
    connect(m_connection, &ConnectionViewModel::errorChanged, this,
            &DeviceTabViewModel::updateState);

    if (m_context.pages != nullptr) {
        for (const PageDescriptor& page : m_context.pages->pages()) {
            if (page.global) {
                continue;
            }
            QObject* viewModel = page.createViewModel ? page.createViewModel(this) : nullptr;
            if (viewModel != nullptr && viewModel->parent() == nullptr) {
                viewModel->setParent(this); // Qt parent ownership (contract: parent = tab)
            }
            m_pageViewModels.append(viewModel);
            m_pageIds.append(page.id);
            if (m_currentPage.isEmpty()) {
                m_currentPage = page.id;
            }
        }
    }
}

DeviceTabViewModel::~DeviceTabViewModel() { shutdown(); }

DeviceTabViewModel::State DeviceTabViewModel::state() const { return m_state; }

bool DeviceTabViewModel::hasError() const { return m_hasError; }

QString DeviceTabViewModel::title() const {
    if (m_choosing || !m_serial) {
        return tr("New device");
    }
    const QString name = m_profile != nullptr ? toQString(m_profile->displayName) : QString();
    return QStringLiteral("%1 %2 · %3").arg(name, m_targetId).arg(*m_serial);
}

void DeviceTabViewModel::setCurrentPage(const QString& pageId) {
    if (pageId == m_currentPage || !m_pageIds.contains(pageId)) {
        return;
    }
    m_currentPage = pageId;
    currentPageChanged(); // signal
}

QString DeviceTabViewModel::busyPage() const { return m_busy ? m_busy->pageId : QString(); }

QString DeviceTabViewModel::busyText() const { return m_busy ? m_busy->text : QString(); }

QString DeviceTabViewModel::busyConfirmText() const {
    return m_busy ? m_busy->confirmText : QString();
}

QObject* DeviceTabViewModel::pageViewModel(const QString& pageId) const {
    const auto index = m_pageIds.indexOf(pageId);
    return index >= 0 ? m_pageViewModels.at(index) : nullptr;
}

device::DeviceSession* DeviceTabViewModel::session() const { return m_session.get(); }

const device::DeviceProfile& DeviceTabViewModel::profile() const { return *m_profile; }

QString DeviceTabViewModel::profileId() const {
    return m_profile != nullptr ? toQString(m_profile->id) : QString();
}

QString DeviceTabViewModel::consoleTargetId() const { return m_targetId; }

const std::shared_ptr<device::ConsoleLog>& DeviceTabViewModel::consoleLog() const {
    return m_consoleLog;
}

bool DeviceTabViewModel::configure(const QString& profileId, const QString& targetId,
                                   std::uint32_t serial, const QString& product) {
    const device::DeviceProfile* profile = device::ProfileRegistry::find(toStdString(profileId));
    if (profile == nullptr || device::findTarget(*profile, toStdString(targetId)) == nullptr ||
        serial == 0) {
        qCWarning(lcGui) << "Tab: unknown configuration" << profileId << targetId << serial;
        return false;
    }
    if (m_session && m_serial != serial) {
        releaseSession();
    }
    m_profile = profile;
    m_targetId = targetId;
    m_serial = serial;
    m_product = product;
    m_choosing = false;
    m_connection->setProduct(product);
    m_connection->setOpenError(std::nullopt);
    m_connection->loadOptions(m_serial, *device::findTarget(*profile, toStdString(targetId)));
    m_picker->setActive(false);
    titleChanged();         // signal
    configurationChanged(); // signal
    updateState();
    return true;
}

void DeviceTabViewModel::setConsoleTarget(const QString& targetId) {
    if (targetId == m_targetId || isBusy() || (m_session && !sessionStopped()) ||
        m_profile == nullptr) {
        return;
    }
    const device::TargetProfile* target = device::findTarget(*m_profile, toStdString(targetId));
    if (target == nullptr) {
        return;
    }
    releaseSession(); // a stopped session of the old target (S1)
    m_targetId = targetId;
    m_connection->loadOptions(m_serial, *target);
    m_connection->setOpenError(std::nullopt);
    titleChanged();         // signal
    configurationChanged(); // signal
    updateState();
}

bool DeviceTabViewModel::sessionStopped() const {
    return m_session && m_session->connection().state == device::ConnectionState::Stopped;
}

Status DeviceTabViewModel::ensureSession() {
    if (m_busy) {
        return std::unexpected(makeError(ErrorCode::InternalBusy, "the tab is busy"));
    }
    if (m_session && !sessionStopped()) {
        return {};
    }
    // A stopped session (fatal error) is replaced by a new one (S1).
    releaseSession();
    return openSession();
}

Status DeviceTabViewModel::openSession() {
    if (m_choosing || !m_serial || m_profile == nullptr) {
        return std::unexpected(makeError(ErrorCode::UsageInvalidArgument, "no probe chosen"));
    }
    const std::string targetId = toStdString(m_targetId);
    const device::TargetProfile* target = device::findTarget(*m_profile, targetId);
    if (target == nullptr) {
        return std::unexpected(
            makeError(ErrorCode::UsageInvalidArgument, "unknown target '" + targetId + "'"));
    }
    if (m_context.registry == nullptr) {
        return std::unexpected(makeError(ErrorCode::InternalUnexpected, "no session registry"));
    }
    const jlink::LocatorOptions locatorOptions =
        m_context.locatorOptions ? m_context.locatorOptions() : jlink::LocatorOptions{};
    auto location = jlink::locateLibrary(locatorOptions);
    if (!location) {
        return std::unexpected(std::move(location).error());
    }

    const ConnectionOptions& options = m_connection->options();
    device::SessionOptions sessionOptions;
    sessionOptions.profile = m_profile;
    sessionOptions.targetId = target->id;
    sessionOptions.probeSn = m_serial;
    sessionOptions.speedKhz = options.speedKhz;
    sessionOptions.latencyMs = options.latencyMs;
    sessionOptions.autoReconnect = options.autoReconnect;
    sessionOptions.resetOnOpen = options.resetOnOpen;
    // A target without an RTT console (CHESTER lte) connects without RTT.
    sessionOptions.attachRtt = !target->rttTerminal.empty();
    // nullopt = the default console file; an empty path = none.
    if (!options.logToFile) {
        sessionOptions.consoleFile = std::filesystem::path();
    }
    sessionOptions.consoleLog = m_consoleLog;

    auto opened =
        device::DeviceSession::open(*m_context.registry, *location, std::move(sessionOptions));
    if (!opened) {
        return std::unexpected(std::move(opened).error());
    }
    m_session = std::move(*opened);
    connect(m_session.get(), &device::DeviceSession::connectionChanged, this,
            &DeviceTabViewModel::onConnectionChanged);
    connect(m_session.get(), &device::DeviceSession::warning, this,
            [this](const Error& warning) { m_connection->setWarning(warning); });
    m_connection->touchRememberedProbe();
    m_connection->setOpenError(std::nullopt);
    m_connection->setConnection(m_session->connection());
    sessionChanged(); // signal
    updateState();
    return {};
}

void DeviceTabViewModel::releaseSession() {
    if (!m_session) {
        return;
    }
    m_session->disconnect(this);
    m_session->close(); // joins the worker, releases the probe lock and the registry slot
    m_retiredSessions.push_back(std::move(m_session));
    QMetaObject::invokeMethod(this, [this] { m_retiredSessions.clear(); }, Qt::QueuedConnection);
    m_connection->setConnection(std::nullopt);
    sessionChanged(); // signal
    updateState();
}

void DeviceTabViewModel::connectDevice() {
    if (m_choosing || isBusy()) {
        return;
    }
    if (m_session) {
        const device::ConnectionState current = m_session->connection().state;
        if (current == device::ConnectionState::Connected ||
            current == device::ConnectionState::Connecting) {
            return;
        }
        // A stopped or failed session is replaced by a fresh one (new options apply).
        releaseSession();
    }
    if (Status status = openSession(); !status) {
        qCInfo(lcGui) << "Connect failed:" << toQString(idOf(status.error().code))
                      << toQString(status.error().detail);
        addMarker(tr("— disconnected: %1 —").arg(ConnectionViewModel::shortCause(status.error())));
        m_connection->setOpenError(std::move(status).error());
        updateState();
    }
}

void DeviceTabViewModel::disconnectDevice() {
    if (isBusy()) {
        return;
    }
    const bool hadSession = static_cast<bool>(m_session);
    releaseSession();
    m_connection->setOpenError(std::nullopt);
    if (hadSession) {
        addMarker(tr("— disconnected —"));
    }
    updateState();
}

void DeviceTabViewModel::reconnect() {
    if (isBusy() || m_choosing) {
        return;
    }
    if (m_session && m_session->connection().state == device::ConnectionState::Disconnected) {
        if (const Status status = m_session->reconnect(); status) {
            return;
        }
    }
    if (m_session && m_session->connection().state == device::ConnectionState::Connected) {
        if (const Status status = m_session->reconnect(); status) {
            return;
        }
    }
    connectDevice();
}

void DeviceTabViewModel::chooseAnotherProbe() {
    if (isBusy()) {
        return;
    }
    releaseSession();
    m_connection->setOpenError(std::nullopt);
    m_choosing = true;
    titleChanged();         // signal
    configurationChanged(); // signal
    updateState();
    m_picker->updateFromScanner();
}

void DeviceTabViewModel::onConnectionChanged(const device::ConnectionEvent& event) {
    m_connection->setConnection(event);
    switch (event.state) {
    case device::ConnectionState::Connected:
        addMarker(tr("— connected —"));
        break;
    case device::ConnectionState::Disconnected:
    case device::ConnectionState::Stopped:
        addMarker(
            event.error
                ? tr("— disconnected: %1 —").arg(ConnectionViewModel::shortCause(*event.error))
                : tr("— disconnected —"));
        break;
    case device::ConnectionState::Connecting:
        break;
    }
    updateState();
}

void DeviceTabViewModel::addMarker(const QString& text) {
    m_consoleLog->appendMarker(device::Channel::Shell, toStdString(text));
    m_consoleLog->appendMarker(device::Channel::Log, toStdString(text));
}

void DeviceTabViewModel::setBusy(const QString& pageId, QString busyText, QString confirmText,
                                 std::function<void()> cancel) {
    if (m_busy && m_busy->pageId != pageId) {
        qCWarning(lcGui) << "Tab: page" << pageId << "cannot be busy while" << m_busy->pageId
                         << "is busy; ignored";
        return;
    }
    const bool wasBusy = m_busy.has_value();
    m_busy = BusyState{.pageId = pageId,
                       .text = std::move(busyText),
                       .confirmText = std::move(confirmText),
                       .cancel = std::move(cancel),
                       .cancelled = false};
    if (!wasBusy) {
        addMarker(tr("— paused: %1 —").arg(m_busy->text));
    }
    busyChanged(); // signal
    m_connection->refresh();
    updateState();
}

void DeviceTabViewModel::clearBusy(const QString& pageId) {
    if (!m_busy || m_busy->pageId != pageId) {
        return;
    }
    m_busy.reset();
    if (!m_closePending) {
        addMarker(tr("— resumed —"));
    }
    busyChanged(); // signal
    m_connection->refresh();
    updateState();
    if (m_closePending) {
        // Queued: the page may call clearBusy() inside one of the session's signals.
        QMetaObject::invokeMethod(this, [this] { closeReady(); }, Qt::QueuedConnection);
    }
}

void DeviceTabViewModel::cancelBusy(bool close) {
    if (!m_busy) {
        return;
    }
    if (close) {
        m_closePending = true;
    }
    if (!m_busy->cancelled) {
        m_busy->cancelled = true;
        if (m_busy->cancel) {
            // A copy: the callback may call clearBusy(), which resets m_busy.
            const std::function<void()> cancel = m_busy->cancel;
            cancel();
        }
    }
}

// NOLINTNEXTLINE(readability-make-member-function-const): page contract (plan 0004)
void DeviceTabViewModel::showToast(QString text) {
    if (m_context.showToast) {
        m_context.showToast(std::move(text));
    }
}

void DeviceTabViewModel::shutdown() {
    if (m_picker != nullptr) {
        m_picker->setActive(false);
    }
    if (m_session) {
        // Pages drop their pointers on sessionChanged (contract), before the session is deleted.
        std::unique_ptr<device::DeviceSession> session = std::move(m_session);
        session->disconnect(this);
        session->close();
        sessionChanged(); // signal
    }
    m_retiredSessions.clear();
}

void DeviceTabViewModel::retranslate() {
    titleChanged(); // signal
    m_connection->refresh();
    m_picker->retranslate();
}

void DeviceTabViewModel::updateState() {
    State state = Disconnected;
    bool error = false;
    if (m_choosing) {
        state = ChoosingDevice;
    } else if (m_busy) {
        state = Busy;
    } else if (m_session) {
        switch (m_session->connection().state) {
        case device::ConnectionState::Connecting:
        case device::ConnectionState::Disconnected: // auto-reconnecting
            state = Connecting;
            break;
        case device::ConnectionState::Connected:
            state = Connected;
            break;
        case device::ConnectionState::Stopped:
            state = Disconnected;
            break;
        }
    }
    if (!m_choosing) {
        error = m_connection->error().valid();
    }
    if (state != m_state || error != m_hasError) {
        m_state = state;
        m_hasError = error;
        stateChanged(); // signal
    }
}

} // namespace hio::gui
