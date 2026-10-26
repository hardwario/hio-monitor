#include "viewmodels/find_filter_engine.h"

#include "hio/device/console_line.h"
#include "hio/device/console_log.h"

#include <QObject>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>
#include <QString>
#include <QStringLiteral>
#include <QStringView>
#include <QTimer>
#include <QtCore/qnamespace.h>
#include <QtTypes>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace hio::gui {

std::expected<TextMatcher, QString> TextMatcher::compile(const TextQuery& query) {
    TextMatcher matcher;
    matcher.m_text = query.text;
    matcher.m_case = query.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    if (query.regex && !query.text.isEmpty()) {
        QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
        if (!query.caseSensitive) {
            options |= QRegularExpression::CaseInsensitiveOption;
        }
        QRegularExpression regex(query.text, options);
        if (!regex.isValid()) {
            return std::unexpected(regex.errorString());
        }
        regex.optimize();
        matcher.m_regex = std::move(regex);
    }
    return matcher;
}

bool TextMatcher::matches(QStringView text) const {
    if (m_text.isEmpty()) {
        return true;
    }
    if (m_regex) {
        return m_regex->matchView(text).hasMatch();
    }
    return text.contains(m_text, m_case);
}

bool TextMatcher::hasVisibleMatch(QStringView text) const {
    if (m_text.isEmpty()) {
        return false;
    }
    if (!m_regex) {
        return text.contains(m_text, m_case);
    }
    const QRegularExpressionMatch match = m_regex->matchView(text);
    if (!match.hasMatch()) {
        return false;
    }
    return match.capturedLength() > 0 || !ranges(text, 1).empty();
}

std::vector<MatchRange> TextMatcher::ranges(QStringView text, std::size_t maxRanges) const {
    std::vector<MatchRange> result;
    if (m_text.isEmpty() || maxRanges == 0) {
        return result;
    }
    if (m_regex) {
        QRegularExpressionMatchIterator it = m_regex->globalMatchView(text);
        while (it.hasNext() && result.size() < maxRanges) {
            const QRegularExpressionMatch match = it.next();
            if (match.capturedLength() > 0) { // empty matches (e.g. "a*") highlight nothing
                result.push_back(
                    MatchRange{.start = match.capturedStart(), .length = match.capturedLength()});
            }
        }
        return result;
    }
    qsizetype from = 0;
    while (result.size() < maxRanges) {
        const qsizetype found = text.indexOf(m_text, from, m_case);
        if (found < 0) {
            break;
        }
        result.push_back(MatchRange{.start = found, .length = m_text.size()});
        from = found + m_text.size();
    }
    return result;
}

int levelBit(device::LogLevel level) noexcept {
    switch (level) {
    case device::LogLevel::Debug:
        return level_bits::debug;
    case device::LogLevel::Info:
        return level_bits::info;
    case device::LogLevel::Warning:
        return level_bits::warning;
    case device::LogLevel::Error:
        return level_bits::error;
    case device::LogLevel::None:
        break;
    }
    return 0;
}

bool LineFilter::passes(const device::ConsoleLine& line, QStringView shownText) const {
    if (line.dir == device::Direction::Marker) {
        return true;
    }
    const int bit = levelBit(line.level);
    if (bit != 0 && (levelMask & bit) == 0) {
        return false;
    }
    return text.matches(shownText);
}

QString lineText(const device::ConsoleLine& line) {
    return QString::fromUtf8(line.text.data(), static_cast<qsizetype>(line.text.size()));
}

QString shownText(const device::ConsoleLine& line) {
    QString text = lineText(line);
    if (line.dir == device::Direction::In) {
        text.prepend(QStringLiteral("> "));
    }
    if (text.size() > maxShownChars) {
        text.truncate(maxShownChars);
        text.append(QChar(0x2026)); // …
    }
    return text;
}

bool findMatches(const TextMatcher& find, const device::ConsoleLine& line, QStringView shown) {
    return line.dir != device::Direction::Marker && find.hasVisibleMatch(shown);
}

device::ChannelMask maskOf(device::Channel channel) noexcept {
    return channel == device::Channel::Shell ? device::ChannelMask::Shell
                                             : device::ChannelMask::Log;
}

LineScan::LineScan(std::shared_ptr<const device::ConsoleLog> log, device::Channel channel,
                   QObject* parent)
    : QObject(parent), m_log(std::move(log)), m_channel(channel) {}

void LineScan::start(std::uint64_t afterSeq, LimitProvider limit, SliceHandler onSlice,
                     DoneHandler onDone) {
    ++m_generation;
    m_cursor = afterSeq;
    m_limit = std::move(limit);
    m_onSlice = std::move(onSlice);
    m_onDone = std::move(onDone);
    m_running = true;
    m_slicesRun = 0;
    schedule();
}

void LineScan::cancel() {
    ++m_generation;
    m_running = false;
    m_onSlice = {};
    m_onDone = {};
    m_limit = {};
}

void LineScan::schedule() {
    QTimer::singleShot(0, this, [this, generation = m_generation] { runSlice(generation); });
}

void LineScan::runSlice(std::uint64_t generation) {
    if (generation != m_generation || !m_running) {
        return; // cancelled or replaced by a newer scan
    }
    const std::uint64_t limit = m_limit ? m_limit() : m_log->lastSeq();
    std::vector<device::ConsoleLine> lines;
    if (m_cursor < limit) {
        lines = m_log->linesSince(m_cursor, maskOf(m_channel), m_sliceLines);
    }
    std::size_t count = 0;
    while (count < lines.size() && lines[count].seq <= limit) {
        ++count;
    }
    ++m_slicesRun;
    if (count > 0) {
        m_cursor = lines[count - 1].seq;
        if (m_onSlice) {
            m_onSlice(std::span<const device::ConsoleLine>(lines.data(), count));
        }
        if (generation != m_generation) {
            return; // the handler started a new scan or cancelled this one
        }
    }
    // Done when this slice reached the limit (fewer lines than a full slice, or a line beyond it).
    if (count < m_sliceLines || count < lines.size()) {
        m_running = false;
        DoneHandler done = std::move(m_onDone);
        m_onSlice = {};
        m_onDone = {};
        m_limit = {};
        if (done) {
            done();
        }
        return;
    }
    schedule();
}

} // namespace hio::gui
