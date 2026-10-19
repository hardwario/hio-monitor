#pragma once

// BatchRunner - sends the commands of a batch file to the device one by one
// (plan/features/0005-m3-console.md, C4).
//
// load() reads and validates the whole file before anything is sent: at most maxFileBytes and
// maxCommands commands; empty lines and lines starting with '#' are skipped (UTF-8, "\n" or
// "\r\n"). start() then sends one command per `interval` (200 ms) through the `send` function on
// a QTimer of the GUI thread. A full command queue (internal.queue_full) keeps the command for the
// next tick; any other send error stops the batch with that error. The owner stops it on
// disconnect (stop()).
//
// Bounds: maxCommands commands of at most CommandHistory::maxEntryBytes bytes each.
// Ownership: Qt parent = ConsoleViewModel. GUI thread only.

#include "hio/core/error.h"
#include "hio/core/result.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>

namespace hio::gui {

class BatchRunner : public QObject {
    Q_OBJECT

public:
    static constexpr qint64 maxFileBytes = 1024LL * 1024;
    static constexpr int maxCommands = 10'000;
    static constexpr std::chrono::milliseconds defaultInterval{200};

    using SendFunction = std::function<Status(const std::string&)>;

    explicit BatchRunner(QObject* parent = nullptr);

    // The commands of a batch file. Errors: io.read_failed (missing, no permission, not a file),
    // usage.invalid_argument (more than maxFileBytes or maxCommands; message names the limit).
    // An empty batch (no commands) is usage.invalid_argument as well.
    [[nodiscard]] static Result<QStringList> load(const QString& path);

    // Starts sending (a running batch is stopped first). The first command is sent at once.
    void start(QStringList commands, SendFunction send);
    // Stops without an error (user, disconnect); finished(nullopt) is emitted when it was running.
    void stop();

    [[nodiscard]] bool running() const noexcept { return m_timer.isActive(); }
    [[nodiscard]] int done() const noexcept { return m_next; }
    [[nodiscard]] int total() const noexcept { return static_cast<int>(m_commands.size()); }
    void setInterval(std::chrono::milliseconds interval);

signals:
    void progressChanged();
    // nullopt: completed or stopped; otherwise the error that ended it.
    void finished(const std::optional<hio::Error>& error);

private:
    void tick();
    void end(const std::optional<Error>& error);

    QTimer m_timer;
    QStringList m_commands;
    SendFunction m_send;
    int m_next = 0;
};

} // namespace hio::gui
