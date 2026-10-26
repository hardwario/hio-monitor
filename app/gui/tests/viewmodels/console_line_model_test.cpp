// ConsoleLineModel (plan 0005, C1/C2): coalesced append, eviction, follow counter, pause and
// resume, clear, markers, filter and search (incl. 100 000 lines, invalid regex keeping the last
// valid result), match ranges on the shown text, selection and copy limits, saving the view, and
// the review round 1 fixes (K1 resume during a scan, K2 currentMatchMoved, K7 evicted rows, the
// model contract under eviction / clear / rebuild with QAbstractItemModelTester).

#include "gui_test_support.h"
#include "hio/device/console_line.h"
#include "hio/device/console_log.h"
#include "viewmodels/console_line_model.h"
#include "viewmodels/find_filter_engine.h"

#include <QAbstractItemModelTester>
#include <QBuffer>
#include <QIODevice>
#include <QList>
#include <QMetaType>
#include <QModelIndex>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QVariant>
#include <QVariantList>
#include <QtContainerFwd>
#include <QtTypes>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace {

using hio::device::Channel;
using hio::device::ChannelMask;
using hio::device::ConsoleLine;
using hio::device::ConsoleLog;
using hio::device::Direction;
using hio::device::LogLevel;
using hio::gui::ConsoleLineModel;
using hio::gui::test::waitFor;
namespace level_bits = hio::gui::level_bits;

class ConsoleLineModelTest : public ::testing::Test {
protected:
    void useLog(std::size_t capacity) {
        m_log = std::make_shared<ConsoleLog>(capacity);
        m_model = std::make_unique<ConsoleLineModel>(m_log, Channel::Log);
    }

    void SetUp() override { useLog(ConsoleLog::defaultCapacityPerChannel); }

    std::uint64_t add(const std::string& text, LogLevel level = LogLevel::Info,
                      Direction dir = Direction::Out, Channel channel = Channel::Log) {
        ConsoleLine line;
        line.channel = channel;
        line.dir = dir;
        line.level = level;
        line.time = std::chrono::system_clock::now();
        line.text = text;
        return m_log->append(std::move(line));
    }

    [[nodiscard]] QString text(int row) const {
        return m_model->data(m_model->index(row), ConsoleLineModel::TextRole).toString();
    }

    [[nodiscard]] bool scanDone() const {
        return waitFor([this] { return !m_model->searching(); });
    }

    [[nodiscard]] ConsoleLog& log() const { return *m_log; }
    [[nodiscard]] ConsoleLineModel& model() const { return *m_model; }

private:
    std::shared_ptr<ConsoleLog> m_log;
    std::unique_ptr<ConsoleLineModel> m_model;
};

TEST_F(ConsoleLineModelTest, AppendsOnRefreshOnlyAndReadsItsChannel) {
    add("one");
    add("shell", LogLevel::None, Direction::Out, Channel::Shell);
    add("two", LogLevel::Warning);
    EXPECT_EQ(model().rowCount(), 0); // coalesced: nothing until refresh()
    const QSignalSpy inserted(&model(), &ConsoleLineModel::rowsInserted);
    model().refresh();
    EXPECT_EQ(inserted.count(), 1); // one update for the batch
    ASSERT_EQ(model().rowCount(), 2);
    EXPECT_EQ(text(0), QStringLiteral("one"));
    EXPECT_EQ(text(1), QStringLiteral("two"));
    EXPECT_EQ(model().data(model().index(1), ConsoleLineModel::LevelTagRole).toString(),
              QStringLiteral("W"));
    EXPECT_EQ(model().totalLines(), 2);
    model().refresh(); // nothing new
    EXPECT_EQ(inserted.count(), 1);
}

TEST_F(ConsoleLineModelTest, SentCommandsAndLongLines) {
    add("app config show", LogLevel::None, Direction::In);
    add(std::string(5000, 'x'));
    model().refresh();
    EXPECT_EQ(text(0), QStringLiteral("> app config show"));
    EXPECT_EQ(text(1).size(), ConsoleLineModel::maxDisplayChars + 1); // cut + "…"
    EXPECT_TRUE(text(1).endsWith(QChar(0x2026)));
}

TEST_F(ConsoleLineModelTest, EvictionRemovesRowsFromTheTop) {
    useLog(10);
    for (int i = 0; i < 8; ++i) {
        add("line " + std::to_string(i));
    }
    model().refresh();
    ASSERT_EQ(model().rowCount(), 8);
    for (int i = 8; i < 15; ++i) {
        add("line " + std::to_string(i));
    }
    const QSignalSpy removed(&model(), &ConsoleLineModel::rowsRemoved);
    model().refresh();
    EXPECT_EQ(removed.count(), 1);
    ASSERT_EQ(model().rowCount(), 10);
    EXPECT_EQ(text(0), QStringLiteral("line 5"));
    EXPECT_EQ(text(9), QStringLiteral("line 14"));
}

TEST_F(ConsoleLineModelTest, FollowCounterAndPause) {
    add("a");
    model().refresh();
    model().setFollowing(false);
    add("b");
    add("c");
    model().refresh();
    EXPECT_EQ(model().newLinesWhileNotFollowing(), 2);
    model().setFollowing(true);
    EXPECT_EQ(model().newLinesWhileNotFollowing(), 0);

    model().setPaused(true);
    add("d");
    add("e");
    model().refresh();
    EXPECT_EQ(model().rowCount(), 3); // frozen
    EXPECT_EQ(model().pendingLines(), 2);
    model().setPaused(false);
    EXPECT_EQ(model().rowCount(), 5);
    EXPECT_EQ(model().pendingLines(), 0);
    EXPECT_EQ(text(4), QStringLiteral("e"));
}

TEST_F(ConsoleLineModelTest, ResumeAppendsOnlyWhatTheLogStillHas) {
    useLog(5);
    add("a");
    model().refresh();
    model().setPaused(true);
    for (int i = 0; i < 12; ++i) {
        add("p" + std::to_string(i));
    }
    model().refresh();
    model().setPaused(false);
    ASSERT_EQ(model().rowCount(), 5); // the model bound = the log's
    EXPECT_EQ(text(0), QStringLiteral("p7"));
}

TEST_F(ConsoleLineModelTest, ClearRemovesTheRows) {
    add("a");
    add("b");
    model().refresh();
    log().clear(ChannelMask::Log);
    model().resetRows();
    EXPECT_EQ(model().rowCount(), 0);
    add("c");
    model().refresh();
    ASSERT_EQ(model().rowCount(), 1);
    EXPECT_EQ(text(0), QStringLiteral("c"));
    // A clear without resetRows() is found by the next refresh (the rows were evicted).
    log().clear(ChannelMask::Log);
    model().refresh();
    EXPECT_EQ(model().rowCount(), 0);
}

TEST_F(ConsoleLineModelTest, FilterHidesRowsButNotMarkers) {
    add("<dbg> therm: 23.8 C", LogLevel::Debug);
    add("<inf> cloud: Uplink started", LogLevel::Info);
    log().appendMarker(Channel::Log, "— disconnected —");
    add("<err> cloud: Uplink timeout", LogLevel::Error);
    model().refresh();
    ASSERT_EQ(model().rowCount(), 4);

    model().setFilterText(QStringLiteral("uplink"));
    EXPECT_TRUE(model().filterActive());
    ASSERT_TRUE(scanDone());
    EXPECT_EQ(model().visibleLines(), 3); // 2 matches + the marker
    EXPECT_EQ(model().totalLines(), 4);
    model().setLevelMask(level_bits::all & ~level_bits::error);
    ASSERT_TRUE(scanDone());
    EXPECT_EQ(model().visibleLines(), 2);
    // New lines are filtered as they arrive.
    add("<inf> cloud: Uplink done", LogLevel::Info);
    add("<inf> other", LogLevel::Info);
    model().refresh();
    EXPECT_EQ(model().visibleLines(), 3);
    model().clearFilter();
    ASSERT_TRUE(scanDone());
    EXPECT_FALSE(model().filterActive());
    EXPECT_EQ(model().visibleLines(), 6);
}

// ADR-0046: an invalid regular expression shows the error and keeps the last valid result.
TEST_F(ConsoleLineModelTest, InvalidRegexKeepsTheLastValidResult) {
    add("ctr_lte: attach");
    add("other");
    log().appendMarker(Channel::Log, "— connected —");
    model().refresh();
    model().setFilterRegex(true);
    model().setFilterText(QStringLiteral("ctr_(lte)"));
    ASSERT_TRUE(scanDone());
    EXPECT_EQ(model().visibleLines(), 2); // the match + the marker
    model().setFilterText(QStringLiteral("ctr_("));
    EXPECT_FALSE(model().searching()); // nothing is rescanned
    EXPECT_FALSE(model().filterError().isEmpty());
    EXPECT_EQ(model().filterText(), QStringLiteral("ctr_(")); // the field keeps what was typed
    EXPECT_EQ(model().visibleLines(), 2);                     // the last valid filter
    add("ctr_lte: detach");
    add("other 2");
    model().refresh();
    EXPECT_EQ(model().visibleLines(), 3); // new lines use the last valid filter too
    // The level chips still apply on top of the last valid text.
    model().setLevelMask(level_bits::all & ~level_bits::info);
    ASSERT_TRUE(scanDone());
    EXPECT_EQ(model().visibleLines(), 1); // the marker
    model().setLevelMask(level_bits::all);
    ASSERT_TRUE(scanDone());
    model().setFilterText(QStringLiteral("other"));
    ASSERT_TRUE(scanDone());
    EXPECT_TRUE(model().filterError().isEmpty());
    EXPECT_EQ(model().visibleLines(), 3); // "other", "other 2" + the marker

    model().clearFilter();
    ASSERT_TRUE(scanDone());
    model().setFindRegex(true);
    model().setFindText(QStringLiteral("attach|detach"));
    ASSERT_TRUE(scanDone());
    ASSERT_EQ(model().matchCount(), 2);
    model().findNext();
    model().setFindText(QStringLiteral("[a"));
    EXPECT_FALSE(model().findError().isEmpty());
    EXPECT_EQ(model().matchCount(), 2); // the last valid matches
    EXPECT_EQ(model().currentMatch(), 1);
    EXPECT_FALSE(model()
                     .data(model().index(0), ConsoleLineModel::MatchRangesRole)
                     .toList()
                     .isEmpty());                         // still highlighted
    model().setFindText(QStringLiteral("attach|detach")); // valid again, unchanged: no rescan
    EXPECT_TRUE(model().findError().isEmpty());
    EXPECT_FALSE(model().searching());
    EXPECT_EQ(model().currentMatch(), 1);
}

TEST_F(ConsoleLineModelTest, SearchStepsThroughMatchesAndStopsFollowing) {
    add("Uplink 1");
    add("other");
    log().appendMarker(Channel::Log, "— uplink marker —");
    add("uplink 2");
    model().refresh();
    model().setFindText(QStringLiteral("uplink"));
    ASSERT_TRUE(scanDone());
    EXPECT_EQ(model().matchCount(), 2); // the marker never matches
    EXPECT_EQ(model().currentMatch(), 0);
    EXPECT_TRUE(model().following());
    model().findNext();
    EXPECT_EQ(model().currentMatch(), 1);
    EXPECT_EQ(model().currentMatchRow(), 0);
    EXPECT_FALSE(model().following()); // moving to a match stops following
    model().findNext();
    EXPECT_EQ(model().currentMatchRow(), 3);
    model().findNext(); // wraps
    EXPECT_EQ(model().currentMatchRow(), 0);
    model().findPrevious();
    EXPECT_EQ(model().currentMatchRow(), 3);
    EXPECT_TRUE(model().data(model().index(3), ConsoleLineModel::CurrentMatchRole).toBool());
    // New matching lines are counted as they arrive; the stream never pauses.
    add("UPLINK 3");
    model().refresh();
    EXPECT_EQ(model().matchCount(), 3);
    EXPECT_EQ(model().rowCount(), 5);
    model().setFindText(QString());
    ASSERT_TRUE(scanDone());
    EXPECT_EQ(model().matchCount(), 0);
}

// ADR-0045 / review K4, K5: plain text only; the matches are character ranges of the shown text.
TEST_F(ConsoleLineModelTest, MatchRangesAreRangesOfTheShownText) {
    add("<b>uplink</b> & uplink");
    add("no match");
    add("uplink now", LogLevel::None, Direction::In); // shown as "> uplink now"
    add(std::string(5000, 'x') + "uplink");           // the match is beyond the cut
    model().refresh();
    model().setFindText(QStringLiteral("uplink"));
    ASSERT_TRUE(scanDone());
    EXPECT_EQ(model().matchCount(), 2); // the hidden match is not counted (K5)
    const auto ranges = [this](int row) {
        QList<std::pair<int, int>> result;
        for (const QVariant& range :
             model().data(model().index(row), ConsoleLineModel::MatchRangesRole).toList()) {
            const QVariantMap map = range.toMap();
            result.append({map.value(QStringLiteral("start")).toInt(),
                           map.value(QStringLiteral("length")).toInt()});
        }
        return result;
    };
    EXPECT_EQ(ranges(0), (QList<std::pair<int, int>>{{3, 6}, {16, 6}}));
    EXPECT_EQ(text(0), QStringLiteral("<b>uplink</b> & uplink")); // plain, unescaped
    EXPECT_TRUE(ranges(1).isEmpty());
    EXPECT_EQ(ranges(2), (QList<std::pair<int, int>>{{2, 6}})); // after the "> " prefix
    EXPECT_TRUE(ranges(3).isEmpty());
    // The sent command's prefix is part of what is searched (it is shown).
    model().setFindText(QStringLiteral("> up"));
    ASSERT_TRUE(scanDone());
    EXPECT_EQ(model().matchCount(), 1);
    // A regular expression that matches only empty text has nothing to highlight: no match.
    model().setFindRegex(true);
    model().setFindText(QStringLiteral("q*"));
    ASSERT_TRUE(scanDone());
    EXPECT_EQ(model().matchCount(), 0);
}

TEST_F(ConsoleLineModelTest, FilterAndSearchOver100000Lines) {
    for (int i = 0; i < 100'000; ++i) {
        add("<inf> app: sample " + std::to_string(i),
            i % 10 == 0 ? LogLevel::Warning : LogLevel::Info);
    }
    model().refresh();
    ASSERT_EQ(model().rowCount(), 100'000);
    model().setFilterText(QStringLiteral("sample 9"));
    EXPECT_TRUE(model().searching());
    ASSERT_TRUE(scanDone());
    EXPECT_EQ(model().visibleLines(), 11'111); // 9, 90-99, 900-999, 9000-9999, 90000-99999
    EXPECT_GE(model().scan().slicesRun(), 20U);
    model().setFindText(QStringLiteral("sample 99"));
    ASSERT_TRUE(scanDone());
    EXPECT_EQ(model().matchCount(), 1111);
    model().setLevelMask(level_bits::warning);
    ASSERT_TRUE(scanDone());
    EXPECT_EQ(model().visibleLines(), 1111);
    EXPECT_EQ(model().matchCount(), 111);
}

TEST_F(ConsoleLineModelTest, SelectionAndCopy) {
    for (int i = 0; i < 5; ++i) {
        add("line " + std::to_string(i));
    }
    model().refresh();
    EXPECT_FALSE(model().hasSelection());
    model().select(1, false);
    model().select(3, true);
    EXPECT_TRUE(model().data(model().index(2), ConsoleLineModel::SelectedRole).toBool());
    EXPECT_FALSE(model().data(model().index(4), ConsoleLineModel::SelectedRole).toBool());
    auto copied = model().selectedText(false);
    EXPECT_EQ(copied.text, QStringLiteral("line 1\nline 2\nline 3"));
    EXPECT_FALSE(copied.truncated);
    copied = model().selectedText(true);
    EXPECT_TRUE(copied.text.split(QLatin1Char('\n')).at(0).endsWith(QStringLiteral(" line 1")));
    EXPECT_EQ(copied.text.split(QLatin1Char('\n')).at(0).size(),
              QStringLiteral("HH:mm:ss.zzz line 1").size());
    // The selection follows its lines when rows are filtered.
    model().setFilterText(QStringLiteral("line [13]"));
    model().setFilterRegex(true);
    ASSERT_TRUE(scanDone());
    EXPECT_EQ(model().selectedText(false).text, QStringLiteral("line 1\nline 3"));
    model().selectAll();
    EXPECT_EQ(model().selectedText(false).lines, 2U);
    model().clearSelection();
    EXPECT_EQ(model().selectedText(false).lines, 0U);
}

TEST_F(ConsoleLineModelTest, CopyIsCappedAt16MiB) {
    const std::string long4k(4000, 'y');
    for (int i = 0; i < 2500; ++i) {
        add(long4k);
    }
    model().refresh();
    model().selectAll();
    const auto copied = model().selectedText(false);
    EXPECT_TRUE(copied.truncated);
    EXPECT_LT(copied.lines, 2500U);
    EXPECT_LE(copied.text.size() * 2, ConsoleLineModel::maxCopyBytes);
}

TEST_F(ConsoleLineModelTest, WritesTheView) {
    add("a");
    add("b", LogLevel::Debug);
    add("c");
    model().refresh();
    model().setLevelMask(level_bits::all & ~level_bits::debug);
    ASSERT_TRUE(scanDone());
    QBuffer buffer;
    ASSERT_TRUE(buffer.open(QIODevice::WriteOnly));
    ASSERT_TRUE(model().writeRows(buffer, false).has_value());
    EXPECT_EQ(buffer.data(), QByteArray("a\nc\n"));
}

// Review K1: Resume while a filter scan runs; the lines the scan had put aside become rows, and
// later Pause / Resume keep the rows sorted (every lookup relies on it).
TEST_F(ConsoleLineModelTest, ResumeDuringAScanKeepsTheRowsSorted) {
    for (int i = 0; i < 20; ++i) {
        add("line " + std::to_string(i));
    }
    model().refresh();
    model().setPaused(true);
    for (int i = 20; i < 10'020; ++i) {
        add("line " + std::to_string(i));
    }
    model().refresh();
    ASSERT_EQ(model().pendingLines(), 10'000);
    model().scan().setSliceLines(5);
    model().setFilterText(QStringLiteral("line"));
    ASSERT_TRUE(waitFor([this] { return model().scan().slicesRun() >= 6; })); // in the pending part
    ASSERT_TRUE(model().searching());
    model().setPaused(false);
    ASSERT_TRUE(scanDone());
    EXPECT_EQ(model().pendingLines(), 0);
    EXPECT_EQ(model().visibleLines(), 10'020);
    model().setPaused(true);
    add("line late");
    model().refresh();
    model().setPaused(false);
    ASSERT_EQ(model().visibleLines(), 10'021);
    qulonglong previous = 0;
    for (int row = 0; row < model().rowCount(); ++row) {
        const qulonglong seq =
            model().data(model().index(row), ConsoleLineModel::SeqRole).toULongLong();
        ASSERT_GT(seq, previous) << "row " << row;
        previous = seq;
    }
    EXPECT_EQ(text(10'020), QStringLiteral("line late"));
    model().select(10'019, false);
    EXPECT_EQ(model().selectedText(false).text, QStringLiteral("line 10019"));
}

// Review K2: only Find next / previous moves the view to the current match.
TEST_F(ConsoleLineModelTest, OnlyFindNextMovesTheView) {
    useLog(50);
    for (int i = 0; i < 10; ++i) {
        add(i % 2 == 0 ? "match " + std::to_string(i) : "other");
    }
    model().refresh();
    model().setFindText(QStringLiteral("match"));
    ASSERT_TRUE(scanDone());
    QSignalSpy moved(&model(), &ConsoleLineModel::currentMatchMoved);
    model().findNext();
    model().findNext();
    ASSERT_EQ(moved.count(), 2);
    EXPECT_EQ(moved.at(1).at(0).toInt(), 2);
    for (int i = 0; i < 60; ++i) { // appends and evictions (the log keeps 50)
        add("match more");
        model().refresh();
    }
    EXPECT_EQ(moved.count(), 2);
    model().findPrevious();
    EXPECT_EQ(moved.count(), 3);
}

// Review K7: a row whose line left the log since the last refresh gives values of the role's type.
TEST_F(ConsoleLineModelTest, EvictedRowsGiveTypedEmptyValues) {
    useLog(5);
    for (int i = 0; i < 5; ++i) {
        add("a");
    }
    model().refresh();
    for (int i = 0; i < 5; ++i) {
        add("b"); // evicts every row; no refresh yet
    }
    const QModelIndex first = model().index(0);
    for (const int role :
         {ConsoleLineModel::TextRole, ConsoleLineModel::TimeRole, ConsoleLineModel::LevelTagRole}) {
        const QVariant value = model().data(first, role);
        EXPECT_EQ(value.typeId(), QMetaType::QString) << role;
        EXPECT_TRUE(value.toString().isEmpty()) << role;
    }
    EXPECT_EQ(model().data(first, ConsoleLineModel::LevelRole).typeId(), QMetaType::Int);
    EXPECT_EQ(model().data(first, ConsoleLineModel::DirectionRole).typeId(), QMetaType::Int);
    EXPECT_EQ(model().data(first, ConsoleLineModel::MatchRangesRole).typeId(),
              QMetaType::QVariantList);
}

// The model contract (rows, signals, data) while rows are evicted, cleared and rebuilt.
TEST_F(ConsoleLineModelTest, ModelContractUnderEvictionClearAndRebuild) {
    useLog(200);
    const QAbstractItemModelTester tester(&model(),
                                          QAbstractItemModelTester::FailureReportingMode::Fatal);
    for (int round = 0; round < 6; ++round) {
        for (int i = 0; i < 90; ++i) {
            add(i % 3 == 0 ? "<wrn> w " + std::to_string(i) : "<inf> i " + std::to_string(i),
                i % 3 == 0 ? LogLevel::Warning : LogLevel::Info);
        }
        model().refresh(); // appends; from the third round on also evicts from the top
    }
    EXPECT_EQ(model().rowCount(), 200);
    model().setFilterText(QStringLiteral("w"));
    model().setFindText(QStringLiteral("1"));
    ASSERT_TRUE(scanDone()); // rebuild (model reset)
    model().select(0, false);
    model().selectAll();
    model().findNext();
    model().setPaused(true);
    for (int i = 0; i < 300; ++i) {
        add("<wrn> w paused", LogLevel::Warning);
    }
    model().refresh();
    model().setPaused(false); // drops evicted rows, appends what the log still has
    model().setLevelMask(level_bits::warning);
    ASSERT_TRUE(scanDone());
    log().clear(ChannelMask::Log);
    model().refresh(); // a clear found by refresh (rows removed from the top)
    EXPECT_EQ(model().rowCount(), 0);
    add("<wrn> w again", LogLevel::Warning);
    model().refresh();
    log().clear(ChannelMask::Log);
    model().resetRows(); // Clear in the view model
    EXPECT_EQ(model().rowCount(), 0);
    model().clearFilter();
    model().setFindText(QString());
    ASSERT_TRUE(scanDone());
}

} // namespace
