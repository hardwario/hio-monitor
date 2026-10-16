#include "viewmodels/console_line_model.h"

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/console_line.h"
#include "hio/device/console_log.h"
#include "viewmodels/find_filter_engine.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QIODevice>
#include <QList>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QStringView>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtTypes>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace hio::gui {

namespace {

// Lines read from the log per chunk when copying or saving rows.
constexpr std::size_t copyChunkLines = 5000;

// Removes the leading elements < `oldest` (the deque is ascending); returns how many.
std::size_t dropBefore(std::deque<std::uint64_t>& seqs, std::uint64_t oldest) {
    const auto end = std::ranges::lower_bound(seqs, oldest);
    const auto count = static_cast<std::size_t>(std::distance(seqs.begin(), end));
    seqs.erase(seqs.begin(), end);
    return count;
}

bool containsSeq(const std::deque<std::uint64_t>& seqs, std::uint64_t seq) {
    return std::ranges::binary_search(seqs, seq);
}

QString timeText(std::chrono::system_clock::time_point time) {
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
    return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss.zzz"));
}

QString levelTag(device::LogLevel level) {
    switch (level) {
    case device::LogLevel::Debug:
        return QStringLiteral("D");
    case device::LogLevel::Info:
        return QStringLiteral("I");
    case device::LogLevel::Warning:
        return QStringLiteral("W");
    case device::LogLevel::Error:
        return QStringLiteral("E");
    case device::LogLevel::None:
        break;
    }
    return {};
}

int toInt(std::size_t value) {
    return static_cast<int>(std::min<std::size_t>(value, std::numeric_limits<int>::max()));
}

// The value of a role for a row whose line left the log since the last refresh (review K7): the
// delegates' required properties get a value of their type, never undefined.
QVariant evictedValue(int role) {
    switch (role) {
    case ConsoleLineModel::TextRole:
    case ConsoleLineModel::TimeRole:
    case ConsoleLineModel::LevelTagRole:
        return QString();
    case ConsoleLineModel::LevelRole:
    case ConsoleLineModel::DirectionRole:
        return 0;
    case ConsoleLineModel::MatchRangesRole:
        return QVariantList();
    default:
        break;
    }
    return {};
}

} // namespace

ConsoleLineModel::ConsoleLineModel(std::shared_ptr<device::ConsoleLog> log, device::Channel channel,
                                   QObject* parent)
    : QAbstractListModel(parent), m_log(std::move(log)), m_channel(channel),
      // Qt parent ownership: the scan is a child of this model.
      m_scan(new LineScan(m_log, channel, this)) { // NOLINT(cppcoreguidelines-owning-memory)
}

int ConsoleLineModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : toInt(m_rows.size());
}

QHash<int, QByteArray> ConsoleLineModel::roleNames() const {
    return {{TextRole, "text"},
            {LevelRole, "level"},
            {DirectionRole, "direction"},
            {TimeRole, "time"},
            {SeqRole, "seq"},
            {MatchesRole, "matches"},
            {SelectedRole, "selected"},
            {CurrentMatchRole, "currentMatch"},
            {MatchRangesRole, "matchRanges"},
            {LevelTagRole, "levelTag"}};
}

const device::ConsoleLine* ConsoleLineModel::lineFor(std::uint64_t seq) const {
    if (m_cached && m_cached->seq == seq) {
        return &*m_cached;
    }
    if (seq == 0) {
        return nullptr;
    }
    std::vector<device::ConsoleLine> lines = m_log->linesSince(seq - 1, maskOf(m_channel), 1);
    if (lines.empty() || lines.front().seq != seq) {
        return nullptr; // evicted or cleared since the last refresh
    }
    m_cached = std::move(lines.front());
    return &*m_cached;
}

QVariant ConsoleLineModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 ||
        static_cast<std::size_t>(index.row()) >= m_rows.size()) {
        return {};
    }
    const std::uint64_t seq = m_rows[static_cast<std::size_t>(index.row())];
    const bool current =
        m_currentMatch >= 0 && m_matches[static_cast<std::size_t>(m_currentMatch)] == seq;
    switch (role) {
    case SeqRole:
        return QVariant::fromValue(static_cast<qulonglong>(seq));
    case SelectedRole:
        return m_selection && seq >= m_selection->first && seq <= m_selection->last;
    case MatchesRole:
        return containsSeq(m_matches, seq);
    case CurrentMatchRole:
        return current;
    default:
        break;
    }
    const device::ConsoleLine* line = lineFor(seq);
    if (line == nullptr) {
        return evictedValue(role);
    }
    switch (role) {
    case TextRole:
        return shownText(*line);
    case LevelRole:
        return static_cast<int>(line->level);
    case LevelTagRole:
        return levelTag(line->level);
    case DirectionRole:
        return static_cast<int>(line->dir);
    case TimeRole:
        return timeText(line->time);
    case MatchRangesRole:
        return containsSeq(m_matches, seq) ? matchRanges(*line) : QVariantList();
    default:
        break;
    }
    return {};
}

QVariantList ConsoleLineModel::matchRanges(const device::ConsoleLine& line) const {
    // Ranges of the shown text (the text the row displays; review K5), drawn by the view as
    // rectangles behind the plain text (ADR-0045).
    QVariantList result;
    for (const MatchRange& range : m_find.ranges(shownText(line))) {
        result.append(QVariantMap{{QStringLiteral("start"), static_cast<int>(range.start)},
                                  {QStringLiteral("length"), static_cast<int>(range.length)}});
    }
    return result;
}

std::uint64_t ConsoleLineModel::oldestSeq() const {
    if (m_log->size(m_channel) == 0) {
        return m_log->lastSeq() + 1;
    }
    const std::vector<device::ConsoleLine> first = m_log->linesSince(0, maskOf(m_channel), 1);
    return first.empty() ? m_log->lastSeq() + 1 : first.front().seq;
}

int ConsoleLineModel::rowOf(std::uint64_t seq) const {
    const auto it = std::ranges::lower_bound(m_rows, seq);
    if (it == m_rows.end() || *it != seq) {
        return -1;
    }
    return toInt(static_cast<std::size_t>(std::distance(m_rows.begin(), it)));
}

int ConsoleLineModel::totalLines() const { return toInt(m_log->size(m_channel)); }

void ConsoleLineModel::refresh() {
    const std::uint64_t oldest = oldestSeq();
    dropEvicted(oldest);
    if (m_cursor >= m_log->lastSeq()) {
        return;
    }
    const std::vector<device::ConsoleLine> lines = m_log->linesSince(m_cursor, maskOf(m_channel));
    m_cursor = m_log->lastSeq();
    if (lines.empty()) {
        return;
    }
    std::vector<std::uint64_t> seqs;
    std::vector<std::uint64_t> matches;
    seqs.reserve(lines.size());
    for (const device::ConsoleLine& line : lines) {
        const QString text = shownText(line);
        if (!m_filter.passes(line, text)) { // markers always pass
            continue;
        }
        seqs.push_back(line.seq);
        if (findMatches(m_find, line, text)) {
            matches.push_back(line.seq);
        }
    }
    if (m_paused) {
        m_pending.insert(m_pending.end(), seqs.begin(), seqs.end());
        m_pendingMatches.insert(m_pendingMatches.end(), matches.begin(), matches.end());
    } else {
        appendRows(seqs, matches);
    }
    countsChanged(); // signal
}

void ConsoleLineModel::appendRows(std::span<const std::uint64_t> seqs,
                                  std::span<const std::uint64_t> matches) {
    if (seqs.empty()) {
        return;
    }
    const int first = toInt(m_rows.size());
    beginInsertRows(QModelIndex(), first, first + toInt(seqs.size()) - 1);
    m_rows.insert(m_rows.end(), seqs.begin(), seqs.end());
    endInsertRows();
    if (!matches.empty()) {
        m_matches.insert(m_matches.end(), matches.begin(), matches.end());
        matchesChanged(); // signal
    }
    if (!m_following) {
        // Saturating (review K6): a very long run without following never overflows.
        const std::size_t added =
            std::min<std::size_t>(seqs.size(), std::numeric_limits<int>::max());
        m_newWhileNotFollowing = toInt(static_cast<std::size_t>(m_newWhileNotFollowing) + added);
    }
    rowsAppended(toInt(seqs.size())); // signal
}

void ConsoleLineModel::dropEvicted(std::uint64_t oldest) {
    if (m_cached && m_cached->seq < oldest) {
        m_cached.reset();
    }
    const auto end = std::ranges::lower_bound(m_rows, oldest);
    const auto rows = static_cast<std::size_t>(std::distance(m_rows.begin(), end));
    if (rows > 0) {
        beginRemoveRows(QModelIndex(), 0, toInt(rows) - 1);
        m_rows.erase(m_rows.begin(), end);
        endRemoveRows();
    }
    dropBefore(m_pending, oldest);
    dropBefore(m_pendingMatches, oldest);
    dropBefore(m_scanRows, oldest);
    dropBefore(m_scanPending, oldest);
    dropBefore(m_scanMatches, oldest);
    const std::size_t matches = dropBefore(m_matches, oldest);
    if (matches > 0) {
        m_currentMatch = m_currentMatch < toInt(matches) ? -1 : m_currentMatch - toInt(matches);
        matchesChanged(); // signal
    }
    if (m_selection && m_selection->first < oldest) {
        if (m_selection->last < oldest) {
            m_selection.reset();
        } else {
            m_selection->first = oldest;
            m_selection->anchor = std::max(m_selection->anchor, oldest);
        }
        selectionChanged(); // signal
    }
    if (rows > 0) {
        countsChanged(); // signal
    }
}

void ConsoleLineModel::resetRows() {
    m_scan->cancel();
    beginResetModel();
    m_rows.clear();
    m_pending.clear();
    m_matches.clear();
    m_pendingMatches.clear();
    m_scanRows.clear();
    m_scanPending.clear();
    m_scanMatches.clear();
    m_scanPendingMatches.clear();
    m_currentMatch = -1;
    m_selection.reset();
    m_cached.reset();
    m_newWhileNotFollowing = 0;
    m_cursor = m_log->lastSeq();
    endResetModel();
    countsChanged();    // signal
    matchesChanged();   // signal
    selectionChanged(); // signal
    searchingChanged(); // signal
}

void ConsoleLineModel::setFollowing(bool following) {
    if (following == m_following) {
        return;
    }
    m_following = following;
    if (m_following) {
        m_newWhileNotFollowing = 0;
        countsChanged(); // signal
    }
    followingChanged(); // signal
}

void ConsoleLineModel::setPaused(bool paused) {
    if (paused == m_paused) {
        return;
    }
    m_paused = paused;
    if (m_paused) {
        m_pausedAt = m_cursor;
    } else {
        // Review K1: a running scan has put the lines after m_pausedAt aside; they are rows now.
        // They follow the scan's rows (ascending seqs), and lines scanned from now on follow
        // them, so the scan's results stay sorted.
        m_scanRows.insert(m_scanRows.end(), m_scanPending.begin(), m_scanPending.end());
        m_scanMatches.insert(m_scanMatches.end(), m_scanPendingMatches.begin(),
                             m_scanPendingMatches.end());
        m_scanPending.clear();
        m_scanPendingMatches.clear();
        dropEvicted(oldestSeq());
        const std::vector<std::uint64_t> seqs(m_pending.begin(), m_pending.end());
        const std::vector<std::uint64_t> matches(m_pendingMatches.begin(), m_pendingMatches.end());
        m_pending.clear();
        m_pendingMatches.clear();
        appendRows(seqs, matches);
    }
    pausedChanged(); // signal
    countsChanged(); // signal
}

bool ConsoleLineModel::filterActive() const {
    return !m_filterQuery.text.isEmpty() || (m_levelMask & level_bits::all) != level_bits::all;
}

void ConsoleLineModel::setFilterText(const QString& text) {
    if (text == m_filterQuery.text) {
        return;
    }
    m_filterQuery.text = text;
    applyFilterQuery();
}

void ConsoleLineModel::setFilterRegex(bool regex) {
    if (regex == m_filterQuery.regex) {
        return;
    }
    m_filterQuery.regex = regex;
    applyFilterQuery();
}

void ConsoleLineModel::setFilterCase(bool caseSensitive) {
    if (caseSensitive == m_filterQuery.caseSensitive) {
        return;
    }
    m_filterQuery.caseSensitive = caseSensitive;
    applyFilterQuery();
}

void ConsoleLineModel::setLevelMask(int mask) {
    mask &= level_bits::all;
    if (mask == m_levelMask) {
        return;
    }
    m_levelMask = mask;
    applyFilterQuery();
}

void ConsoleLineModel::clearFilter() {
    if (m_filterQuery.text.isEmpty() && m_levelMask == level_bits::all) {
        return;
    }
    m_filterQuery.text.clear();
    m_levelMask = level_bits::all;
    applyFilterQuery();
}

void ConsoleLineModel::applyFilterQuery() {
    bool rescan = m_filter.levelMask != m_levelMask;
    m_filter.levelMask = m_levelMask;
    if (auto compiled = TextMatcher::compile(m_filterQuery); compiled) {
        m_filterError.clear();
        if (m_filterQuery != m_filterApplied) {
            m_filterApplied = m_filterQuery;
            m_filter.text = std::move(*compiled);
            rescan = true;
        }
    } else {
        // ADR-0046: the field shows the error, the view keeps the last valid filter.
        m_filterError = std::move(compiled).error();
    }
    filterChanged(); // signal
    if (rescan) {
        startScan(true);
    }
}

void ConsoleLineModel::setFindText(const QString& text) {
    if (text == m_findQuery.text) {
        return;
    }
    m_findQuery.text = text;
    applyFindQuery();
}

void ConsoleLineModel::setFindRegex(bool regex) {
    if (regex == m_findQuery.regex) {
        return;
    }
    m_findQuery.regex = regex;
    applyFindQuery();
}

void ConsoleLineModel::setFindCase(bool caseSensitive) {
    if (caseSensitive == m_findQuery.caseSensitive) {
        return;
    }
    m_findQuery.caseSensitive = caseSensitive;
    applyFindQuery();
}

void ConsoleLineModel::applyFindQuery() {
    auto compiled = TextMatcher::compile(m_findQuery);
    if (!compiled) {
        // ADR-0046: the field shows the error, the view keeps the last valid matches.
        m_findError = std::move(compiled).error();
        findChanged(); // signal
        return;
    }
    m_findError.clear();
    const bool changed = m_findQuery != m_findApplied;
    if (changed) {
        m_findApplied = m_findQuery;
        m_find = std::move(*compiled);
    }
    findChanged(); // signal
    if (changed) {
        // A running row rebuild also recomputes the matches.
        startScan(m_scan->running() && m_scanRebuildsRows);
    }
}

void ConsoleLineModel::startScan(bool rebuildRows) {
    m_scanRebuildsRows = rebuildRows;
    m_scanRows.clear();
    m_scanPending.clear();
    m_scanMatches.clear();
    m_scanPendingMatches.clear();
    const bool searchable = !m_find.isEmpty();
    if (!rebuildRows && !searchable) {
        // Nothing to search: no scan needed.
        m_scan->cancel();
        const bool had = !m_matches.empty() || !m_pendingMatches.empty() || m_currentMatch >= 0;
        m_matches.clear();
        m_pendingMatches.clear();
        m_currentMatch = -1;
        if (had) {
            emitAllRowsChanged({MatchesRole, CurrentMatchRole, MatchRangesRole});
        }
        matchesChanged();   // signal
        searchingChanged(); // signal
        return;
    }
    m_scan->start(
        0, [this] { return m_cursor; },
        [this, searchable](std::span<const device::ConsoleLine> lines) {
            scanSlice(lines, searchable);
        },
        [this] { finishScan(); });
    searchingChanged(); // signal
}

void ConsoleLineModel::scanSlice(std::span<const device::ConsoleLine> lines, bool searchable) {
    for (const device::ConsoleLine& line : lines) {
        const QString text = shownText(line);
        if (!m_filter.passes(line, text)) { // markers always pass
            continue;
        }
        const bool pending = m_paused && line.seq > m_pausedAt;
        if (m_scanRebuildsRows) {
            (pending ? m_scanPending : m_scanRows).push_back(line.seq);
        }
        if (searchable && findMatches(m_find, line, text)) {
            (pending ? m_scanPendingMatches : m_scanMatches).push_back(line.seq);
        }
    }
}

void ConsoleLineModel::finishScan() {
    if (!m_paused) { // nothing is put aside while the view runs (see setPaused)
        m_scanRows.insert(m_scanRows.end(), m_scanPending.begin(), m_scanPending.end());
        m_scanMatches.insert(m_scanMatches.end(), m_scanPendingMatches.begin(),
                             m_scanPendingMatches.end());
        m_scanPending.clear();
        m_scanPendingMatches.clear();
    }
    const std::uint64_t oldest = oldestSeq();
    dropBefore(m_scanRows, oldest);
    dropBefore(m_scanPending, oldest);
    dropBefore(m_scanMatches, oldest);
    dropBefore(m_scanPendingMatches, oldest);
    if (m_scanRebuildsRows) {
        beginResetModel();
        m_rows = std::move(m_scanRows);
        m_pending = std::move(m_scanPending);
        m_matches = std::move(m_scanMatches);
        m_pendingMatches = std::move(m_scanPendingMatches);
        m_currentMatch = -1;
        m_cached.reset();
        endResetModel();
        countsChanged(); // signal
    } else {
        m_matches = std::move(m_scanMatches);
        m_pendingMatches = std::move(m_scanPendingMatches);
        m_currentMatch = -1;
        emitAllRowsChanged({MatchesRole, CurrentMatchRole, MatchRangesRole});
    }
    m_scanRows.clear();
    m_scanPending.clear();
    m_scanMatches.clear();
    m_scanPendingMatches.clear();
    matchesChanged();   // signal
    searchingChanged(); // signal
}

int ConsoleLineModel::currentMatchRow() const {
    if (m_currentMatch < 0 || static_cast<std::size_t>(m_currentMatch) >= m_matches.size()) {
        return -1;
    }
    return rowOf(m_matches[static_cast<std::size_t>(m_currentMatch)]);
}

void ConsoleLineModel::findNext() {
    if (m_matches.empty()) {
        return;
    }
    const int count = toInt(m_matches.size());
    moveToMatch(m_currentMatch < 0 ? 0 : (m_currentMatch + 1) % count);
}

void ConsoleLineModel::findPrevious() {
    if (m_matches.empty()) {
        return;
    }
    const int count = toInt(m_matches.size());
    moveToMatch(m_currentMatch < 0 ? count - 1 : (m_currentMatch - 1 + count) % count);
}

void ConsoleLineModel::moveToMatch(int index) {
    const int oldRow = currentMatchRow();
    m_currentMatch = index;
    const int newRow = currentMatchRow();
    const QList<int> roles{CurrentMatchRole};
    if (oldRow >= 0) {
        dataChanged(this->index(oldRow), this->index(oldRow), roles); // signal
    }
    if (newRow >= 0) {
        dataChanged(this->index(newRow), this->index(newRow), roles); // signal
    }
    setFollowing(false); // moving to a match stops following (ADR-0046)
    matchesChanged();    // signal
    if (newRow >= 0) {
        currentMatchMoved(newRow); // signal: only here the view scrolls to the match (K2)
    }
}

void ConsoleLineModel::emitAllRowsChanged(const QList<int>& roles) {
    if (!m_rows.empty()) {
        dataChanged(index(0), index(toInt(m_rows.size()) - 1), roles); // signal
    }
}

void ConsoleLineModel::select(int row, bool extend) {
    if (row < 0 || static_cast<std::size_t>(row) >= m_rows.size()) {
        return;
    }
    const std::uint64_t seq = m_rows[static_cast<std::size_t>(row)];
    const std::optional<Selection> old = m_selection;
    if (extend && m_selection) {
        m_selection->first = std::min(m_selection->anchor, seq);
        m_selection->last = std::max(m_selection->anchor, seq);
    } else {
        m_selection = Selection{.anchor = seq, .first = seq, .last = seq};
    }
    std::uint64_t first = m_selection->first;
    std::uint64_t last = m_selection->last;
    if (old) {
        first = std::min(first, old->first);
        last = std::max(last, old->last);
    }
    const auto from = std::ranges::lower_bound(m_rows, first);
    const auto to = std::ranges::upper_bound(m_rows, last);
    if (from < to) {
        dataChanged(index(toInt(static_cast<std::size_t>(from - m_rows.begin()))),
                    index(toInt(static_cast<std::size_t>(to - m_rows.begin())) - 1),
                    {SelectedRole}); // signal
    }
    selectionChanged(); // signal
}

void ConsoleLineModel::selectAll() {
    if (m_rows.empty()) {
        return;
    }
    m_selection =
        Selection{.anchor = m_rows.front(), .first = m_rows.front(), .last = m_rows.back()};
    emitAllRowsChanged({SelectedRole});
    selectionChanged(); // signal
}

void ConsoleLineModel::clearSelection() {
    if (!m_selection) {
        return;
    }
    m_selection.reset();
    emitAllRowsChanged({SelectedRole});
    selectionChanged(); // signal
}

QString ConsoleLineModel::formatLine(const device::ConsoleLine& line, bool withTime) {
    QString text = lineText(line);
    if (line.dir == device::Direction::In) {
        text.prepend(QStringLiteral("> "));
    }
    if (withTime) {
        text.prepend(timeText(line.time) + QLatin1Char(' '));
    }
    return text;
}

namespace {

// Calls `visit` for the lines of the rows [first, last) (indices into `rows`), in order, until it
// returns false. Reads the log in chunks; rows that are no longer in the log are skipped.
void forEachRow(const device::ConsoleLog& log, device::Channel channel,
                const std::deque<std::uint64_t>& rows, std::size_t first, std::size_t last,
                const std::function<bool(const device::ConsoleLine&)>& visit) {
    std::size_t row = first;
    while (row < last) {
        const std::vector<device::ConsoleLine> lines =
            log.linesSince(rows[row] - 1, maskOf(channel), copyChunkLines);
        if (lines.empty()) {
            return;
        }
        for (const device::ConsoleLine& line : lines) {
            while (row < last && rows[row] < line.seq) {
                ++row; // evicted row
            }
            if (row >= last) {
                return;
            }
            if (rows[row] == line.seq) {
                if (!visit(line)) {
                    return;
                }
                ++row;
            }
        }
        if (row < last && rows[row] <= lines.back().seq) {
            ++row; // not in the log any more
        }
    }
}

} // namespace

ConsoleLineModel::CopyText ConsoleLineModel::selectedText(bool withTime) const {
    CopyText result;
    if (!m_selection) {
        return result;
    }
    const auto from = std::ranges::lower_bound(m_rows, m_selection->first);
    const auto to = std::ranges::upper_bound(m_rows, m_selection->last);
    if (from >= to) {
        return result;
    }
    forEachRow(*m_log, m_channel, m_rows, static_cast<std::size_t>(from - m_rows.begin()),
               static_cast<std::size_t>(to - m_rows.begin()), [&](const device::ConsoleLine& line) {
                   const QString text = formatLine(line, withTime);
                   const qsizetype bytes = (text.size() + 1) * 2; // UTF-16 in memory
                   if (result.lines >= maxCopyLines ||
                       (result.text.size() * 2) + bytes > maxCopyBytes) {
                       result.truncated = true;
                       return false;
                   }
                   if (result.lines > 0) {
                       result.text += QLatin1Char('\n');
                   }
                   result.text += text;
                   ++result.lines;
                   return true;
               });
    return result;
}

Status ConsoleLineModel::writeRows(QIODevice& device, bool withTime) const {
    bool ok = true;
    forEachRow(*m_log, m_channel, m_rows, 0, m_rows.size(), [&](const device::ConsoleLine& line) {
        QByteArray bytes = formatLine(line, withTime).toUtf8();
        bytes.append('\n');
        ok = device.write(bytes) == bytes.size();
        return ok;
    });
    if (!ok) {
        return std::unexpected(
            makeError(ErrorCode::IoWriteFailed, device.errorString().toStdString()));
    }
    return {};
}

} // namespace hio::gui
