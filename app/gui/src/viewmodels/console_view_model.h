#pragma once

// ConsoleViewModel - the view model of the console page of a tab (plan/features/0005-m3-console.md;
// ADR-0045, ADR-0046, ADR-0056, ADR-0058, ADR-0061, ADR-0062, ADR-0063).
//
// Created by the "console" PageDescriptor for every tab (Qt parent = the DeviceTabViewModel).
// It shows the tab's ConsoleLog in two ConsoleLineModels (shell, log), sends commands, walks and
// searches the command history (shared per profile/target, HistoryStore), runs batch files
// (BatchRunner), resets the device through the session's reset operation (ADR-0062) and saves /
// copies / clears the views.
//
// Session: the tab owns it (0004 contract). This view model keeps it only as QPointer and
// reconnects on DeviceTabViewModel::sessionChanged, which is also emitted on tab close / quit
// before the session is deleted. A Reset without a session calls ensureSession() first.
// Markers ("— connected —", "— paused: … —", …) are added by the shell, never here (review S5).
//
// Updates: linesReceived starts a 50 ms single-shot timer (≤ 20 model updates per second); a
// 250 ms poll catches lines appended without a session signal (the shell's markers).
// Busy (another page runs an operation): the input, Reset, Reconnect and the batch are disabled; a
// running batch stops with "The batch stopped after %1 of %2 commands." (review K9). A running
// batch also stops on disconnect (same message) and quietly on a session change.
// Open log file opens the session's current console file (DeviceSession::consoleFile(), amended
// by 0005), or the last one of this tab while no session is open.
// Session warnings (J-Link firmware, console file) are shown by the shell's banner (PageHost).
// Translated properties follow a live language switch (AppViewModel::languageChanged; K12).
//
// Ownership: Qt parent = the tab; the models, the batch runner and the timers are children.
// GUI thread only.

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/command_tree.h"
#include "hio/device/command_tree_cache.h"
#include "hio/device/command_tree_learner.h"
#include "hio/device/operation.h"
#include "viewmodels/console_line_model.h"
#include "viewmodels/console_settings.h"
#include "viewmodels/error_info.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration>
#include <QtTypes>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>

namespace hio::device {
class ConsoleLog;
class DeviceSession;
struct ConnectionEvent;
struct OperationResult;
} // namespace hio::device

namespace hio::gui {

class AppViewModel;
class BatchRunner;
class DeviceTabViewModel;
class HistoryStore;
class SharedHistory;

struct ConsoleContext {
    ConsoleSettings* settings = nullptr; // AppViewModel's child; outlives the tabs
    HistoryStore* history = nullptr;     // AppViewModel's member; outlives the tabs
    AppViewModel* app = nullptr;         // outlives the tabs; languageChanged (null in tests)
};

class ConsoleViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the console page registration")

    Q_PROPERTY(hio::gui::ConsoleLineModel* shell READ shell CONSTANT)
    Q_PROPERTY(hio::gui::ConsoleLineModel* log READ log CONSTANT)
    Q_PROPERTY(hio::gui::ConsoleSettings* settings READ settings CONSTANT)
    Q_PROPERTY(bool inputEnabled READ inputEnabled NOTIFY stateChanged)
    Q_PROPERTY(QString inputDisabledReason READ inputDisabledReason NOTIFY stateChanged)
    // Connected or connecting: the views say "Waiting for data from the device…" while empty.
    Q_PROPERTY(bool deviceActive READ deviceActive NOTIFY stateChanged)
    // Not connected and no lines: the "Not connected" empty state with [Connect].
    Q_PROPERTY(bool showConnectState READ showConnectState NOTIFY stateChanged)
    Q_PROPERTY(bool canReset READ canReset NOTIFY stateChanged)
    Q_PROPERTY(bool canReconnect READ canReconnect NOTIFY stateChanged)
    Q_PROPERTY(bool canRunBatch READ canRunBatch NOTIFY stateChanged)
    Q_PROPERTY(quint64 droppedLines READ droppedLines NOTIFY droppedLinesChanged)
    Q_PROPERTY(hio::gui::ErrorInfo error READ error NOTIFY errorChanged) // invalid when none
    Q_PROPERTY(bool batchRunning READ batchRunning NOTIFY batchChanged)
    Q_PROPERTY(int batchDone READ batchDone NOTIFY batchChanged)
    Q_PROPERTY(int batchTotal READ batchTotal NOTIFY batchChanged)
    // Both views follow / are paused (the toolbar's Follow and Pause).
    Q_PROPERTY(bool following READ following WRITE setFollowing NOTIFY followingChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(bool swapPanes READ swapPanes WRITE setSwapPanes NOTIFY layoutChanged)
    // The page width (set by the page); below stackBreakpoint the split stacks (ADR-0061).
    Q_PROPERTY(int pageWidth READ pageWidth WRITE setPageWidth NOTIFY layoutChanged)
    Q_PROPERTY(bool stackedLayout READ stackedLayout NOTIFY layoutChanged)
    // 0021 (ADR-0170): the device command tree is being read ("Reading commands from the
    // device…").
    Q_PROPERTY(bool readingCommands READ readingCommands NOTIFY commandTreeChanged)

public:
    static constexpr int stackBreakpoint = 1000; // ADR-0061
    static constexpr std::chrono::milliseconds refreshDelay{50};
    static constexpr std::chrono::milliseconds pollInterval{250};
    static constexpr std::chrono::milliseconds announceInterval{2000};
    static constexpr int historySearchMax = 50;
    static constexpr qsizetype maxAnnouncementChars = 200;

    ConsoleViewModel(DeviceTabViewModel* tab, ConsoleContext context);
    ~ConsoleViewModel() override;
    ConsoleViewModel(const ConsoleViewModel&) = delete;
    ConsoleViewModel& operator=(const ConsoleViewModel&) = delete;
    ConsoleViewModel(ConsoleViewModel&&) = delete;
    ConsoleViewModel& operator=(ConsoleViewModel&&) = delete;

    [[nodiscard]] ConsoleLineModel* shell() const { return m_shell; }
    [[nodiscard]] ConsoleLineModel* log() const { return m_log; }
    [[nodiscard]] ConsoleSettings* settings() const { return m_context.settings; }
    [[nodiscard]] bool inputEnabled() const;
    [[nodiscard]] QString inputDisabledReason() const;
    [[nodiscard]] bool deviceActive() const;
    [[nodiscard]] bool showConnectState() const;
    [[nodiscard]] bool canReset() const;
    [[nodiscard]] bool canReconnect() const;
    [[nodiscard]] bool canRunBatch() const;
    [[nodiscard]] quint64 droppedLines() const noexcept { return m_droppedLines; }
    [[nodiscard]] ErrorInfo error() const { return m_error; }
    [[nodiscard]] bool batchRunning() const;
    [[nodiscard]] int batchDone() const;
    [[nodiscard]] int batchTotal() const;
    [[nodiscard]] bool following() const;
    void setFollowing(bool following);
    [[nodiscard]] bool paused() const;
    void setPaused(bool paused);
    [[nodiscard]] bool swapPanes() const noexcept { return m_swapPanes; }
    void setSwapPanes(bool swap);
    [[nodiscard]] int pageWidth() const noexcept { return m_pageWidth; }
    void setPageWidth(int width);
    [[nodiscard]] bool stackedLayout() const;

    // Sends a command (Enter); adds it to the history. False when it was not sent (input disabled,
    // or the session refused it: the error is shown), so the input keeps the text (review K9).
    Q_INVOKABLE bool send(const QString& line);
    // Up / Down in the input: the previous / next history entry. `current` is the input text:
    // Up keeps it as the draft that Down returns to at the end of the walk; Down while not walking
    // the history returns it unchanged.
    Q_INVOKABLE QString historyPrevious(const QString& current);
    Q_INVOKABLE QString historyNext(const QString& current);
    // Ctrl+R: newest first, unique, case-insensitive substring; ≤ max (50).
    Q_INVOKABLE [[nodiscard]] QStringList historySearch(const QString& text,
                                                        int max = historySearchMax) const;
    // 0021 (ADR-0170), device::Completer over the tab's history and command tree:
    // the automatic list - [{text, label, help, kind: "command" | "history"}], ≤ 50 rows;
    // Asks for the command tree / a command's subcommands when needed (learned asynchronously:
    // commandTreeChanged).
    Q_INVOKABLE [[nodiscard]] QVariantList suggestions(const QString& text);
    // the ghost text source without a list - the newest history entry that extends `text` ("" =
    // none);
    Q_INVOKABLE [[nodiscard]] QString suggestion(const QString& text) const;
    // Tab with the list closed - {replacement, candidates}.
    Q_INVOKABLE [[nodiscard]] QVariantMap complete(const QString& text);
    Q_INVOKABLE void runBatch(const QUrl& file);
    Q_INVOKABLE void stopBatch();
    // channelMask: 1 = shell, 2 = log, 3 = both.
    Q_INVOKABLE void clear(int channelMask);
    // channel: 0 = shell, 1 = log. Writes the rows of that view (filter applied).
    Q_INVOKABLE void saveView(int channel, const QUrl& file);
    // Copies the selected rows of a view (with timestamps when they are shown).
    Q_INVOKABLE void copySelection(int channel);
    Q_INVOKABLE void openLogFile();
    Q_INVOKABLE void openLogFolder() const;
    // session->reset(tab.consoleTargetId(), Go | Halt) (ADR-0062).
    Q_INVOKABLE void reset(bool halt);
    Q_INVOKABLE void reconnect();
    Q_INVOKABLE void connectDevice();
    Q_INVOKABLE void dismissDropped();
    Q_INVOKABLE void dismissError();

    // Tests: reads the log into the models now (normally on the 50 ms timer).
    void refreshNow();
    [[nodiscard]] bool readingCommands() const;
    // The device command tree used for suggestions (nullopt = none known). Normally learned
    // (CommandTreeLearner); tests set it directly.
    void setCommandTree(std::optional<device::CommandTree> tree);
    // Lines the session dropped (DeviceSession::linesDropped; saturating).
    void addDroppedLines(quint64 count);
    [[nodiscard]] BatchRunner& batchRunner() { return *m_batch; }
    // The console file Open log file opens: the session's current one, or the last one of this
    // tab while no session is open (empty when none).
    [[nodiscard]] std::filesystem::path consoleFile() const;

signals:
    void stateChanged();
    void droppedLinesChanged();
    void errorChanged();
    void batchChanged();
    void followingChanged();
    void pausedChanged();
    void layoutChanged();
    void commandTreeChanged();
    // Throttled (≤ 1 per 2 s) text for the screen reader: the newest shell output line.
    void announce(const QString& text);

private:
    void attachSession();
    void startLearning();
    void learnFor(const QString& text);
    void scheduleRefresh();
    void refresh();
    void onConnectionChanged(const device::ConnectionEvent& event);
    void onOperationFinished(device::OperationId id, const device::OperationResult& result);
    void onBatchFinished(const std::optional<Error>& error);
    // Stops a running batch with "The batch stopped after %1 of %2 commands." and `cause`.
    void stopBatchWithError(ErrorCode code, const QString& cause, const QString& suggestion);
    void resetHistoryNavigation();
    void onShellRowsAppended();
    void announceNewest();
    void setError(const Error& error);
    [[nodiscard]] bool busy() const;
    [[nodiscard]] bool targetHasConsole() const;
    [[nodiscard]] ConsoleLineModel* model(int channel) const;

    DeviceTabViewModel* m_tab; // parent
    ConsoleContext m_context;
    std::shared_ptr<device::ConsoleLog> m_consoleLog;
    ConsoleLineModel* m_shell = nullptr; // child
    ConsoleLineModel* m_log = nullptr;   // child
    BatchRunner* m_batch = nullptr;      // child
    QPointer<device::DeviceSession> m_session;
    QTimer m_refreshTimer;
    QTimer m_pollTimer;
    QTimer m_announceTimer;
    bool m_announcePending = false;
    quint64 m_droppedLines = 0;
    ErrorInfo m_error;
    std::optional<device::OperationId> m_resetOperation;
    bool m_resetHalt = false;
    std::shared_ptr<SharedHistory> m_history;
    // 0021 (ADR-0170): the learned tree (immutable snapshots) and how it is learned.
    device::CommandTreeCache m_treeCache;
    device::CommandTreeLearner* m_learner = nullptr; // child
    std::shared_ptr<const device::CommandTree> m_commandTree;
    bool m_learningStarted = false; // for this attach
    std::string m_learnedFirmware;  // fw-name + fw-version it was started for
    // History walk: m_historyIndex == entries().size() means "not navigating". A revision that
    // differs from the history's (another tab added a command, another history, the first use)
    // starts the walk again from the newest entry.
    static constexpr std::uint64_t noRevision = std::numeric_limits<std::uint64_t>::max();
    std::size_t m_historyIndex = 0;
    std::uint64_t m_historyRevision = noRevision;
    QString m_historyDraft;
    std::filesystem::path m_lastConsoleFile; // of the previous session (Open log file)
    bool m_hasLines = false;                 // the log has lines (showConnectState)
    bool m_swapPanes = false;
    int m_pageWidth = 1280;
};

} // namespace hio::gui
