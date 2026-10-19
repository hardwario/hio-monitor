#pragma once

// ConsoleLineModel - one console channel (shell or log) of a tab as a list model for a virtualised
// ListView (plan/features/0005-m3-console.md, C1/C2; ADR-0045, ADR-0046, ADR-0056, ADR-0063).
//
// The model indexes the tab's ConsoleLog (ADR-0063): it keeps only the sequence numbers of its
// rows (8 bytes per row) and reads a line from the log when a row is shown, so the console text is
// not held twice (ADR-0063 option 1). Earlier lines stay across sessions until Clear or eviction.
//
// refresh() (called by ConsoleViewModel at most every 50 ms, i.e. ≤ 20 model updates per second)
// reads the new lines of the channel (ConsoleLog::linesSince with the model's cursor), removes the
// rows evicted from the log (or cleared) from the top and appends the new rows in one update.
// - Follow: `following` is view state (the view scrolls to the end while it is set); while it is
//   off, appended rows are counted in newLinesWhileNotFollowing ("▼ N new lines").
// - Pause freezes the rows: new rows are kept aside (pendingLines) and appended on resume (up to
//   the log's bound: evicted ones are dropped).
// - Filter (text, Aa, .*, level chips) hides rows; search (text, Aa, .*) counts the matching rows
//   and steps through them (findNext / findPrevious, which also emit currentMatchMoved for the
//   view; appends and evictions never move the view). Both are rebuilt by a time-sliced LineScan
//   (5 000 lines per event-loop turn); new lines are matched as they arrive. Matching uses the
//   text the row shows (shownText()). An invalid regular expression sets filterError / findError
//   and keeps the last valid filter / matches (ADR-0046). Marker rows are always shown and never
//   match. Search never pauses the stream; moving to a match turns `following` off.
// - Rows are plain text (ADR-0045): the matches of a row are given as character ranges of its
//   text (matchRanges) and drawn by the view as rectangles; no rich text from device data.
// - Selection is a range of rows (click, Shift+click, Ctrl+A), kept by sequence number across
//   appends; copy is capped at maxCopyLines / maxCopyBytes.
//
// Bounds: rows, pending rows and matches ≤ the log's capacity per channel (100 000); a shown line
// ≤ maxDisplayChars (longer lines are cut with "…"); highlighted ranges ≤ 64 per row;
// newLinesWhileNotFollowing saturates at INT_MAX.
// Ownership: Qt parent = ConsoleViewModel; shares the tab's log. GUI thread only.

#include "hio/core/result.h"
#include "hio/device/console_line.h"
#include "hio/device/console_log.h"
#include "viewmodels/find_filter_engine.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QtQmlIntegration>
#include <QtTypes>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>

class QIODevice;

namespace hio::gui {

class ConsoleLineModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by ConsoleViewModel")

    Q_PROPERTY(int channel READ channelIndex CONSTANT) // 0 = shell, 1 = log
    Q_PROPERTY(bool following READ following WRITE setFollowing NOTIFY followingChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(int newLinesWhileNotFollowing READ newLinesWhileNotFollowing NOTIFY countsChanged)
    Q_PROPERTY(int pendingLines READ pendingLines NOTIFY countsChanged) // while paused
    Q_PROPERTY(int totalLines READ totalLines NOTIFY countsChanged)     // in the log
    Q_PROPERTY(int visibleLines READ visibleLines NOTIFY countsChanged) // rows
    // filter
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterChanged)
    Q_PROPERTY(bool filterRegex READ filterRegex WRITE setFilterRegex NOTIFY filterChanged)
    Q_PROPERTY(bool filterCase READ filterCase WRITE setFilterCase NOTIFY filterChanged)
    Q_PROPERTY(int levelMask READ levelMask WRITE setLevelMask NOTIFY filterChanged)
    Q_PROPERTY(bool filterActive READ filterActive NOTIFY filterChanged)
    Q_PROPERTY(QString filterError READ filterError NOTIFY filterChanged)
    // find
    Q_PROPERTY(QString findText READ findText WRITE setFindText NOTIFY findChanged)
    Q_PROPERTY(bool findRegex READ findRegex WRITE setFindRegex NOTIFY findChanged)
    Q_PROPERTY(bool findCase READ findCase WRITE setFindCase NOTIFY findChanged)
    Q_PROPERTY(int matchCount READ matchCount NOTIFY matchesChanged)
    Q_PROPERTY(int currentMatch READ currentMatch NOTIFY matchesChanged)       // 1-based, 0 = none
    Q_PROPERTY(int currentMatchRow READ currentMatchRow NOTIFY matchesChanged) // -1 = none
    Q_PROPERTY(QString findError READ findError NOTIFY findChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    // selection
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)

public:
    enum Roles : quint16 {           // NOLINT(cppcoreguidelines-use-enum-class)
        TextRole = Qt::UserRole + 1, // shown text ("> " before a sent command), ≤ 4 KiB
        LevelRole,                   // 0 none, 1 D, 2 I, 3 W, 4 E (device::LogLevel)
        DirectionRole,               // 0 out, 1 in (sent command), 2 marker
        TimeRole,                    // "HH:mm:ss.zzz" (local time)
        SeqRole,
        MatchesRole,      // the row matches the search
        SelectedRole,     // the row is selected
        CurrentMatchRole, // the row is the current match
        MatchRangesRole,  // [{start, length}, …] of the matches in `text` (≤ 64); [] when none
        LevelTagRole,     // "D", "I", "W", "E" or ""
    };
    Q_ENUM(Roles)

    static constexpr qsizetype maxDisplayChars = maxShownChars;
    static constexpr std::size_t maxCopyLines = 100'000;
    static constexpr qsizetype maxCopyBytes = 16LL * 1024 * 1024;

    ConsoleLineModel(std::shared_ptr<device::ConsoleLog> log, device::Channel channel,
                     QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] device::Channel channel() const noexcept { return m_channel; }
    [[nodiscard]] int channelIndex() const noexcept {
        return m_channel == device::Channel::Shell ? 0 : 1;
    }

    // Reads the new lines from the log, removes evicted rows, appends new rows (one update).
    void refresh();
    // The newest seq the model has read (its cursor in the log).
    [[nodiscard]] std::uint64_t cursor() const noexcept { return m_cursor; }

    [[nodiscard]] bool following() const noexcept { return m_following; }
    void setFollowing(bool following);
    [[nodiscard]] bool paused() const noexcept { return m_paused; }
    void setPaused(bool paused);
    [[nodiscard]] int newLinesWhileNotFollowing() const noexcept { return m_newWhileNotFollowing; }
    [[nodiscard]] int pendingLines() const noexcept { return static_cast<int>(m_pending.size()); }
    [[nodiscard]] int totalLines() const;
    [[nodiscard]] int visibleLines() const noexcept { return static_cast<int>(m_rows.size()); }

    [[nodiscard]] QString filterText() const { return m_filterQuery.text; }
    void setFilterText(const QString& text);
    [[nodiscard]] bool filterRegex() const noexcept { return m_filterQuery.regex; }
    void setFilterRegex(bool regex);
    [[nodiscard]] bool filterCase() const noexcept { return m_filterQuery.caseSensitive; }
    void setFilterCase(bool caseSensitive);
    [[nodiscard]] int levelMask() const noexcept { return m_levelMask; }
    void setLevelMask(int mask);
    [[nodiscard]] bool filterActive() const;
    [[nodiscard]] QString filterError() const { return m_filterError; }
    Q_INVOKABLE void clearFilter();

    [[nodiscard]] QString findText() const { return m_findQuery.text; }
    void setFindText(const QString& text);
    [[nodiscard]] bool findRegex() const noexcept { return m_findQuery.regex; }
    void setFindRegex(bool regex);
    [[nodiscard]] bool findCase() const noexcept { return m_findQuery.caseSensitive; }
    void setFindCase(bool caseSensitive);
    [[nodiscard]] int matchCount() const noexcept { return static_cast<int>(m_matches.size()); }
    [[nodiscard]] int currentMatch() const noexcept { return m_currentMatch + 1; }
    [[nodiscard]] int currentMatchRow() const;
    [[nodiscard]] QString findError() const { return m_findError; }
    [[nodiscard]] bool searching() const noexcept { return m_scan->running(); }

    // Moves to the next / previous matching row (wraps around) and turns `following` off.
    Q_INVOKABLE void findNext();
    Q_INVOKABLE void findPrevious();

    // Selects `row`; with `extend` from the anchor (Shift+click, drag) to `row`.
    Q_INVOKABLE void select(int row, bool extend);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    [[nodiscard]] bool hasSelection() const noexcept { return m_selection.has_value(); }

    struct CopyText {
        QString text;
        std::size_t lines = 0;
        bool truncated = false; // maxCopyLines / maxCopyBytes reached
    };
    // The selected rows joined with "\n" (with the time column when `withTime`).
    [[nodiscard]] CopyText selectedText(bool withTime) const;
    // Writes every row (the current view, filter applied) to `device`, one line each.
    // Errors: io.write_failed.
    [[nodiscard]] Status writeRows(QIODevice& device, bool withTime) const;

    // Removes every row (after ConsoleLog::clear of this channel).
    void resetRows();

    // Tests: the scan's slice size and its slice count.
    [[nodiscard]] LineScan& scan() { return *m_scan; }

signals:
    void followingChanged();
    void pausedChanged();
    void countsChanged();
    void filterChanged();
    void findChanged();
    void matchesChanged();
    void searchingChanged();
    void selectionChanged();
    // findNext / findPrevious moved to the match in `row` (the view scrolls there; review K2).
    void currentMatchMoved(int row);
    // New shell/log output rows were appended (for the throttled screen reader announcement).
    void rowsAppended(int count);

private:
    struct Selection {
        std::uint64_t anchor = 0;
        std::uint64_t first = 0;
        std::uint64_t last = 0;
    };

    // The line with `seq` (one-line cache); nullopt when it was evicted.
    [[nodiscard]] const device::ConsoleLine* lineFor(std::uint64_t seq) const;
    [[nodiscard]] std::uint64_t oldestSeq() const;
    [[nodiscard]] int rowOf(std::uint64_t seq) const; // -1 when not a row
    [[nodiscard]] static QString formatLine(const device::ConsoleLine& line, bool withTime);
    [[nodiscard]] QVariantList matchRanges(const device::ConsoleLine& line) const;
    void dropEvicted(std::uint64_t oldest);
    void appendRows(std::span<const std::uint64_t> seqs, std::span<const std::uint64_t> matches);
    void applyFilterQuery();
    void applyFindQuery();
    void startScan(bool rebuildRows);
    void scanSlice(std::span<const device::ConsoleLine> lines, bool searchable);
    void finishScan();
    void moveToMatch(int index);
    void emitAllRowsChanged(const QList<int>& roles);

    std::shared_ptr<device::ConsoleLog> m_log;
    device::Channel m_channel;
    std::uint64_t m_cursor = 0;

    std::deque<std::uint64_t> m_rows;           // seqs of the shown rows, ascending
    std::deque<std::uint64_t> m_pending;        // seqs kept aside while paused
    std::deque<std::uint64_t> m_matches;        // seqs of rows matching the search
    std::deque<std::uint64_t> m_pendingMatches; // matches among m_pending
    int m_currentMatch = -1;                    // index into m_matches
    std::optional<Selection> m_selection;

    bool m_following = true;
    bool m_paused = false;
    std::uint64_t m_pausedAt = 0; // cursor when the view was paused
    int m_newWhileNotFollowing = 0;

    // The queries as typed and the ones in effect: an invalid regular expression keeps the last
    // valid query in effect (ADR-0046).
    TextQuery m_filterQuery;
    TextQuery m_filterApplied;
    int m_levelMask = level_bits::all;
    LineFilter m_filter; // compiled from m_filterApplied and m_levelMask
    QString m_filterError;
    TextQuery m_findQuery;
    TextQuery m_findApplied;
    TextMatcher m_find; // compiled from m_findApplied
    QString m_findError;

    // The running scan and its results (applied when it finishes).
    LineScan* m_scan = nullptr; // child
    bool m_scanRebuildsRows = false;
    std::deque<std::uint64_t> m_scanRows;
    std::deque<std::uint64_t> m_scanPending;
    std::deque<std::uint64_t> m_scanMatches;
    std::deque<std::uint64_t> m_scanPendingMatches;

    // One-line cache for data() (a delegate reads several roles of one row).
    mutable std::optional<device::ConsoleLine> m_cached;
};

} // namespace hio::gui
