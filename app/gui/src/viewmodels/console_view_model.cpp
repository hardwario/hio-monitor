#include "viewmodels/console_view_model.h"

#include "gui_log.h"
#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/result.h"
#include "hio/device/command_tree.h"
#include "hio/device/completer.h"
#include "hio/device/console_line.h"
#include "hio/device/console_log.h"
#include "hio/device/device_session.h"
#include "hio/device/operation.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"
#include "viewmodels/app_view_model.h"
#include "viewmodels/connection_view_model.h"
#include "viewmodels/console_line_model.h"
#include "viewmodels/console_settings.h"
#include "viewmodels/device_tab_view_model.h"
#include "viewmodels/error_info.h"
#include "viewmodels/history_store.h"
#include "workers/batch_runner.h"

#include <QChar>
#include <QClipboard>
#include <QDir>
#include <QGuiApplication>
#include <QLatin1Char>
#include <QList>
#include <QLoggingCategory>
#include <QModelIndex>
#include <QObject>
#include <QSaveFile>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtContainerFwd>
#include <QtTypes>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <deque>
#include <expected>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace hio::gui {

namespace {

Error guiError(ErrorCode code, const QString& message, const QString& cause,
               const QString& suggestion, const QString& detail) {
    Error error = makeError(code, detail.toStdString());
    error.message = message.toStdString();
    error.cause = cause.toStdString();
    error.suggestion = suggestion.toStdString();
    return error;
}

} // namespace

ConsoleViewModel::ConsoleViewModel(DeviceTabViewModel* tab, ConsoleContext context)
    : QObject(tab), m_tab(tab), m_context(context), m_consoleLog(tab->consoleLog()),
      // Qt parent ownership: the models and the batch runner are children of this object.
      m_shell(new ConsoleLineModel( // NOLINT(cppcoreguidelines-owning-memory)
          m_consoleLog, device::Channel::Shell, this)),
      m_log(new ConsoleLineModel( // NOLINT(cppcoreguidelines-owning-memory)
          m_consoleLog, device::Channel::Log, this)),
      m_batch(new BatchRunner(this)) { // NOLINT(cppcoreguidelines-owning-memory)
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(refreshDelay);
    connect(&m_refreshTimer, &QTimer::timeout, this, &ConsoleViewModel::refresh);
    m_pollTimer.setInterval(pollInterval);
    connect(&m_pollTimer, &QTimer::timeout, this, [this] {
        if (m_consoleLog->lastSeq() != m_shell->cursor() ||
            m_consoleLog->lastSeq() != m_log->cursor()) {
            refresh();
        }
    });
    m_pollTimer.start();
    m_announceTimer.setSingleShot(true);
    m_announceTimer.setInterval(announceInterval);
    connect(&m_announceTimer, &QTimer::timeout, this, [this] {
        if (m_announcePending) {
            m_announcePending = false;
            announceNewest();
            m_announceTimer.start();
        }
    });

    for (ConsoleLineModel* model : {m_shell, m_log}) {
        connect(model, &ConsoleLineModel::followingChanged, this,
                &ConsoleViewModel::followingChanged);
        connect(model, &ConsoleLineModel::pausedChanged, this, &ConsoleViewModel::pausedChanged);
    }
    connect(m_shell, &ConsoleLineModel::rowsAppended, this, &ConsoleViewModel::onShellRowsAppended);
    connect(m_batch, &BatchRunner::progressChanged, this, &ConsoleViewModel::batchChanged);
    connect(m_batch, &BatchRunner::finished, this, &ConsoleViewModel::onBatchFinished);

    connect(m_tab, &DeviceTabViewModel::sessionChanged, this, &ConsoleViewModel::attachSession);
    connect(m_tab, &DeviceTabViewModel::stateChanged, this, [this] {
        stateChanged(); // signal
        scheduleRefresh();
    });
    connect(m_tab, &DeviceTabViewModel::configurationChanged, this, [this] {
        // Another profile / target: another history, and no walk through the old one (K8).
        m_history.reset();
        resetHistoryNavigation();
        stateChanged(); // signal
    });
    connect(m_tab, &DeviceTabViewModel::busyChanged, this, [this] {
        if (busy()) {
            // The other page owns the session now (review K9: said in the ErrorPanel).
            // internal.busy: the ErrorPanel offers Dismiss, not Reconnect.
            stopBatchWithError(ErrorCode::InternalBusy,
                               tr("another operation started on the device."),
                               tr("Run the batch file again when it has finished."));
        }
        stateChanged(); // signal
        scheduleRefresh();
    });
    if (m_context.settings != nullptr) {
        connect(m_context.settings, &ConsoleSettings::changed, this,
                &ConsoleViewModel::layoutChanged);
    }
    // Translated properties (inputDisabledReason) follow a live language switch (K12: the
    // language manager's signal, forwarded by AppViewModel; no event filter per tab).
    if (m_context.app != nullptr) {
        connect(m_context.app, &AppViewModel::languageChanged, this,
                &ConsoleViewModel::stateChanged);
    }
    attachSession();
    refresh();
}

ConsoleViewModel::~ConsoleViewModel() = default;

// ---- session ------------------------------------------------------------------------------------

void ConsoleViewModel::attachSession() {
    device::DeviceSession* session = m_tab->session();
    if (session == m_session.data()) {
        return;
    }
    if (m_session) {
        // Open log file keeps working after Disconnect (the old session is deleted after this).
        if (std::filesystem::path file = m_session->consoleFile(); !file.empty()) {
            m_lastConsoleFile = std::move(file);
        }
        m_session->disconnect(this);
    }
    m_batch->stop(); // a new (or no) session: the batch belonged to the old one
    m_resetOperation.reset();
    m_session = session;
    if (m_session) {
        connect(m_session, &device::DeviceSession::linesReceived, this,
                &ConsoleViewModel::scheduleRefresh);
        connect(m_session, &device::DeviceSession::connectionChanged, this,
                &ConsoleViewModel::onConnectionChanged);
        connect(m_session, &device::DeviceSession::linesDropped, this,
                &ConsoleViewModel::addDroppedLines);
        connect(m_session, &device::DeviceSession::operationFinished, this,
                &ConsoleViewModel::onOperationFinished);
    }
    stateChanged(); // signal
    scheduleRefresh();
}

void ConsoleViewModel::onConnectionChanged(const device::ConnectionEvent& event) {
    if (event.state != device::ConnectionState::Connected) {
        stopBatchWithError(ErrorCode::TargetNotConnected, tr("the device disconnected."),
                           tr("Reconnect and run the batch file again."));
    }
    stateChanged(); // signal
    scheduleRefresh();
}

void ConsoleViewModel::stopBatchWithError(ErrorCode code, const QString& cause,
                                          const QString& suggestion) {
    if (!m_batch->running()) {
        return;
    }
    const int done = m_batch->done();
    const int total = m_batch->total();
    m_batch->stop();
    setError(guiError(code, tr("The batch stopped after %1 of %2 commands.").arg(done).arg(total),
                      cause, suggestion, QString()));
}

void ConsoleViewModel::onOperationFinished(device::OperationId id,
                                           const device::OperationResult& result) {
    if (!m_resetOperation || *m_resetOperation != id) {
        return; // another page's operation
    }
    m_resetOperation.reset();
    if (!result.status) {
        setError(result.status.error());
    } else {
        m_tab->showToast(m_resetHalt ? tr("The device was reset and halted.")
                                     : tr("The device was reset."));
    }
    stateChanged(); // signal
}

// ---- updates ------------------------------------------------------------------------------------

void ConsoleViewModel::scheduleRefresh() {
    if (!m_refreshTimer.isActive()) {
        m_refreshTimer.start();
    }
}

void ConsoleViewModel::refresh() {
    m_shell->refresh();
    m_log->refresh();
    const bool hasLines = m_consoleLog->size(device::Channel::Shell) > 0 ||
                          m_consoleLog->size(device::Channel::Log) > 0;
    if (hasLines != m_hasLines) {
        m_hasLines = hasLines;
        stateChanged(); // signal: showConnectState
    }
}

void ConsoleViewModel::refreshNow() {
    m_refreshTimer.stop();
    refresh();
}

void ConsoleViewModel::onShellRowsAppended() {
    if (m_announceTimer.isActive()) {
        m_announcePending = true;
        return;
    }
    announceNewest();
    m_announceTimer.start();
}

void ConsoleViewModel::announceNewest() {
    const int rows = m_shell->rowCount();
    if (rows == 0) {
        return;
    }
    const QModelIndex last = m_shell->index(rows - 1);
    if (m_shell->data(last, ConsoleLineModel::DirectionRole).toInt() !=
        static_cast<int>(device::Direction::Out)) {
        return; // the echo of a command or a marker
    }
    announce(m_shell->data(last, ConsoleLineModel::TextRole)
                 .toString()
                 .left(maxAnnouncementChars)); // signal
}

// ---- state --------------------------------------------------------------------------------------

bool ConsoleViewModel::busy() const { return m_tab->isBusy(); }

bool ConsoleViewModel::targetHasConsole() const {
    const device::TargetProfile* target =
        device::findTarget(m_tab->profile(), toStdString(m_tab->consoleTargetId()));
    return target != nullptr && !target->rttTerminal.empty();
}

bool ConsoleViewModel::inputEnabled() const {
    return !busy() && m_session && m_tab->state() == DeviceTabViewModel::Connected &&
           targetHasConsole();
}

QString ConsoleViewModel::inputDisabledReason() const {
    if (inputEnabled()) {
        return {};
    }
    if (busy()) {
        QString text = m_tab->busyText();
        while (text.endsWith(QChar(0x2026)) || text.endsWith(QLatin1Char('.'))) {
            text.chop(1);
        }
        return tr("%1 — the console resumes automatically").arg(text);
    }
    if (!targetHasConsole()) {
        return tr("This target has no console");
    }
    if (m_tab->state() == DeviceTabViewModel::Connecting) {
        return tr("Connecting…");
    }
    return tr("Not connected");
}

bool ConsoleViewModel::deviceActive() const {
    const DeviceTabViewModel::State state = m_tab->state();
    return state == DeviceTabViewModel::Connected || state == DeviceTabViewModel::Connecting ||
           state == DeviceTabViewModel::Busy;
}

bool ConsoleViewModel::showConnectState() const {
    return !deviceActive() && m_tab->state() != DeviceTabViewModel::ChoosingDevice &&
           m_consoleLog->size(device::Channel::Shell) == 0 &&
           m_consoleLog->size(device::Channel::Log) == 0;
}

bool ConsoleViewModel::canReset() const {
    return !busy() && !m_tab->choosing() && !m_resetOperation;
}

bool ConsoleViewModel::canReconnect() const { return !busy() && !m_tab->choosing(); }

bool ConsoleViewModel::canRunBatch() const { return inputEnabled() && !m_batch->running(); }

bool ConsoleViewModel::batchRunning() const { return m_batch->running(); }

int ConsoleViewModel::batchDone() const { return m_batch->done(); }

int ConsoleViewModel::batchTotal() const { return m_batch->total(); }

bool ConsoleViewModel::following() const { return m_shell->following() && m_log->following(); }

void ConsoleViewModel::setFollowing(bool following) {
    m_shell->setFollowing(following);
    m_log->setFollowing(following);
}

bool ConsoleViewModel::paused() const { return m_shell->paused() || m_log->paused(); }

void ConsoleViewModel::setPaused(bool paused) {
    m_shell->setPaused(paused);
    m_log->setPaused(paused);
}

void ConsoleViewModel::setSwapPanes(bool swap) {
    if (swap == m_swapPanes) {
        return;
    }
    m_swapPanes = swap;
    layoutChanged(); // signal
}

void ConsoleViewModel::setPageWidth(int width) {
    if (width == m_pageWidth) {
        return;
    }
    const bool stacked = stackedLayout();
    m_pageWidth = width;
    if (stacked != stackedLayout()) {
        layoutChanged(); // signal
    }
}

bool ConsoleViewModel::stackedLayout() const {
    return (m_context.settings != nullptr && m_context.settings->stacked()) ||
           m_pageWidth < stackBreakpoint;
}

ConsoleLineModel* ConsoleViewModel::model(int channel) const {
    return channel == 0 ? m_shell : m_log;
}

void ConsoleViewModel::setError(const Error& error) {
    m_error = ErrorInfo(error);
    errorChanged(); // signal
}

void ConsoleViewModel::dismissError() {
    if (!m_error.valid()) {
        return;
    }
    m_error = ErrorInfo();
    errorChanged(); // signal
}

void ConsoleViewModel::addDroppedLines(quint64 count) {
    if (count == 0) {
        return;
    }
    m_droppedLines = count > std::numeric_limits<quint64>::max() - m_droppedLines
                         ? std::numeric_limits<quint64>::max()
                         : m_droppedLines + count;
    droppedLinesChanged(); // signal
}

void ConsoleViewModel::dismissDropped() {
    if (m_droppedLines == 0) {
        return;
    }
    m_droppedLines = 0;
    droppedLinesChanged(); // signal
}

// ---- input and history --------------------------------------------------------------------------

bool ConsoleViewModel::send(const QString& line) {
    if (!inputEnabled()) {
        return false;
    }
    if (const Status sent = m_session->sendLine(toStdString(line)); !sent) {
        setError(sent.error()); // e.g. internal.queue_full: the input keeps the command (K9)
        return false;
    }
    if (m_history == nullptr && m_context.history != nullptr) {
        m_history = m_context.history->history(m_tab->profileId(), m_tab->consoleTargetId());
    }
    if (m_history != nullptr) {
        m_history->add(line);
        m_historyIndex = m_history->entries().size();
        m_historyRevision = m_history->revision();
    }
    m_historyDraft.clear();
    setFollowing(true); // the answer is shown
    return true;
}

void ConsoleViewModel::resetHistoryNavigation() {
    m_historyIndex = 0;
    m_historyRevision = noRevision; // the next Up starts from the newest entry
    m_historyDraft.clear();
}

QString ConsoleViewModel::historyPrevious(const QString& current) {
    if (m_history == nullptr && m_context.history != nullptr) {
        m_history = m_context.history->history(m_tab->profileId(), m_tab->consoleTargetId());
    }
    if (m_history == nullptr || m_history->entries().empty()) {
        return current;
    }
    const auto& entries = m_history->entries();
    if (m_history->revision() != m_historyRevision || m_historyIndex > entries.size()) {
        // Another tab added a command: start again from the newest entry.
        m_historyIndex = entries.size();
        m_historyRevision = m_history->revision();
    }
    if (m_historyIndex == entries.size()) {
        m_historyDraft = current;
    }
    if (m_historyIndex > 0) {
        --m_historyIndex;
    }
    return toQString(entries[m_historyIndex]);
}

QString ConsoleViewModel::historyNext(const QString& current) {
    if (m_history == nullptr) {
        return current; // not navigating: Down keeps the typed text
    }
    const auto& entries = m_history->entries();
    if (m_history->revision() != m_historyRevision || m_historyIndex >= entries.size()) {
        // Not navigating (or the history changed under the walk): keep what the input shows.
        m_historyIndex = entries.size();
        m_historyRevision = m_history->revision();
        return current;
    }
    ++m_historyIndex;
    if (m_historyIndex == entries.size()) {
        return m_historyDraft; // the end of the walk: back to the text typed before it
    }
    return toQString(entries[m_historyIndex]);
}

QStringList ConsoleViewModel::historySearch(const QString& text, int max) const {
    if (m_context.history == nullptr) {
        return {};
    }
    // The store's history of this tab's pair (shared with the other tabs).
    const std::shared_ptr<SharedHistory> history =
        m_history != nullptr
            ? m_history
            : m_context.history->history(m_tab->profileId(), m_tab->consoleTargetId());
    return history->search(text, static_cast<std::size_t>(std::clamp(max, 0, historySearchMax)));
}

// ---- suggestions (0021, ADR-0170) ---------------------------------------------------------------

namespace {

const std::deque<std::string>& noHistory() {
    static const std::deque<std::string> empty;
    return empty;
}

} // namespace

QVariantList ConsoleViewModel::suggestions(const QString& text) const {
    const std::shared_ptr<SharedHistory> history =
        m_history != nullptr || m_context.history == nullptr
            ? m_history
            : m_context.history->history(m_tab->profileId(), m_tab->consoleTargetId());
    const device::Completer completer(m_commandTree ? &*m_commandTree : nullptr);
    QVariantList rows;
    for (const device::Suggestion& row :
         completer.suggest(toStdString(text), history ? history->entries() : noHistory())) {
        rows.append(
            QVariantMap{{QStringLiteral("text"), toQString(row.text)},
                        {QStringLiteral("label"), toQString(row.label)},
                        {QStringLiteral("help"), toQString(row.help)},
                        {QStringLiteral("kind"), row.kind == device::Suggestion::Kind::Command
                                                     ? QStringLiteral("command")
                                                     : QStringLiteral("history")}});
    }
    return rows;
}

QString ConsoleViewModel::suggestion(const QString& text) const {
    const std::shared_ptr<SharedHistory> history =
        m_history != nullptr || m_context.history == nullptr
            ? m_history
            : m_context.history->history(m_tab->profileId(), m_tab->consoleTargetId());
    if (!history) {
        return {};
    }
    const auto hint = device::Completer::historyHint(toStdString(text), history->entries());
    return hint ? toQString(*hint) : QString();
}

QVariantMap ConsoleViewModel::complete(const QString& text) const {
    const device::Completer completer(m_commandTree ? &*m_commandTree : nullptr);
    const device::Completion completion = completer.complete(toStdString(text));
    QStringList candidates;
    for (const std::string& name : completion.candidates) {
        candidates.append(toQString(name));
    }
    return {{QStringLiteral("replacement"), toQString(completion.replacement)},
            {QStringLiteral("candidates"), candidates}};
}

void ConsoleViewModel::setCommandTree(std::optional<device::CommandTree> tree) {
    m_commandTree = std::move(tree);
    commandTreeChanged(); // signal
}

// ---- batch --------------------------------------------------------------------------------------

void ConsoleViewModel::runBatch(const QUrl& file) {
    if (!canRunBatch()) {
        return;
    }
    auto commands = BatchRunner::load(file.isLocalFile() ? file.toLocalFile() : file.toString());
    if (!commands) {
        setError(std::move(commands).error());
        return;
    }
    dismissError();
    m_batch->start(std::move(*commands), [this](const std::string& command) -> Status {
        if (!m_session) {
            return std::unexpected(makeError(ErrorCode::TargetNotConnected, "no session"));
        }
        return m_session->sendLine(command);
    });
    batchChanged(); // signal
    stateChanged(); // signal: canRunBatch
}

void ConsoleViewModel::stopBatch() { m_batch->stop(); }

void ConsoleViewModel::onBatchFinished(const std::optional<Error>& error) {
    if (error) {
        setError(*error);
    }
    batchChanged(); // signal
    stateChanged(); // signal
}

// ---- views --------------------------------------------------------------------------------------

void ConsoleViewModel::clear(int channelMask) {
    const bool shell = (channelMask & 1) != 0;
    const bool log = (channelMask & 2) != 0;
    if (!shell && !log) {
        return;
    }
    device::ChannelMask mask = device::ChannelMask::All;
    if (!log) {
        mask = device::ChannelMask::Shell;
    } else if (!shell) {
        mask = device::ChannelMask::Log;
    }
    m_consoleLog->clear(mask);
    if (shell) {
        m_shell->resetRows();
    }
    if (log) {
        m_log->resetRows();
    }
    refresh(); // showConnectState
}

void ConsoleViewModel::saveView(int channel, const QUrl& file) {
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    const auto failed = [this, &path](const QString& reason) {
        setError(guiError(ErrorCode::IoWriteFailed, tr("The file could not be saved."),
                          tr("the folder is not writable or the disk is full."),
                          tr("Choose another folder."), path + QStringLiteral(": ") + reason));
    };
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        failed(out.errorString());
        return;
    }
    const bool withTime = m_context.settings != nullptr && m_context.settings->showTimestamps();
    if (const Status written = model(channel)->writeRows(out, withTime); !written) {
        out.cancelWriting();
        failed(toQString(written.error().detail));
        return;
    }
    if (!out.commit()) {
        failed(out.errorString());
        return;
    }
    m_tab->showToast(tr("The view was saved to %1.").arg(QDir::toNativeSeparators(path)));
}

void ConsoleViewModel::copySelection(int channel) {
    const bool withTime = m_context.settings != nullptr && m_context.settings->showTimestamps();
    const ConsoleLineModel::CopyText copied = model(channel)->selectedText(withTime);
    if (copied.lines == 0) {
        return;
    }
    if (QClipboard* clipboard = QGuiApplication::clipboard(); clipboard != nullptr) {
        clipboard->setText(copied.text);
    }
    if (copied.truncated) {
        m_tab->showToast(tr("Only the first %Ln lines were copied (copy limit).", "",
                            static_cast<int>(copied.lines)));
    }
}

std::filesystem::path ConsoleViewModel::consoleFile() const {
    if (m_session) {
        return m_session->consoleFile(); // empty while not written (yet), see DeviceSession
    }
    return m_lastConsoleFile;
}

void ConsoleViewModel::openLogFile() {
    const std::filesystem::path file = consoleFile();
    if (file.empty()) {
        m_tab->showToast(tr("There is no console log file for this device yet."));
        return;
    }
    if (m_context.settings == nullptr ||
        !m_context.settings->openUrl(QUrl::fromLocalFile(toQString(toUtf8(file))))) {
        qCWarning(lcGui) << "The console log file could not be opened:" << toQString(toUtf8(file));
    }
}

void ConsoleViewModel::openLogFolder() const {
    if (m_context.settings != nullptr) {
        m_context.settings->openLogFolder();
    }
}

// ---- device -------------------------------------------------------------------------------------

void ConsoleViewModel::reset(bool halt) {
    if (!canReset()) {
        return;
    }
    if (!m_session) {
        if (const Status opened = m_tab->ensureSession(); !opened) {
            setError(opened.error());
            return;
        }
        attachSession();
        if (!m_session) {
            return;
        }
    }
    auto started = m_session->reset(toStdString(m_tab->consoleTargetId()),
                                    halt ? device::ResetMode::Halt : device::ResetMode::Go);
    if (!started) {
        setError(std::move(started).error());
        return;
    }
    dismissError();
    m_resetOperation = *started;
    m_resetHalt = halt;
    stateChanged(); // signal
}

void ConsoleViewModel::reconnect() {
    if (canReconnect()) {
        m_tab->connection()->reconnect();
    }
}

void ConsoleViewModel::connectDevice() { m_tab->connection()->connectDevice(); }

} // namespace hio::gui
