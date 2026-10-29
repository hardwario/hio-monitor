#include "hwio/hw/jlink/rtt_channel.h"

#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/log.h"
#include "hwio/core/result.h"
#include "hwio/hw/jlink/clock.h"
#include "hwio/hw/jlink/jlink_api.h"
#include "hwio/hw/jlink/types.h"

#include <QChar>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QString>
#include <QtLogging>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace hwio::jlink {

namespace {

// Review M-6: counts read from the target's RAM; more than kMaxRttBuffers is an invalid block.
bool plausibleCount(int count) { return count <= kMaxRttBuffers; }

std::optional<int> findByName(const std::vector<RttBufferInfo>& infos, const std::string& name) {
    const auto it = std::ranges::find(infos, name, &RttBufferInfo::name);
    if (it == infos.end()) {
        return std::nullopt;
    }
    return static_cast<int>(it - infos.begin());
}

void logBuffers(const std::vector<RttBufferInfo>& buffers, const char* direction) {
    for (std::size_t i = 0; i < buffers.size(); ++i) {
        qCInfo(log::lcJLink).noquote() << QStringLiteral("%1 buffer %2: %3 <Size=%4>")
                                              .arg(QString::fromLatin1(direction))
                                              .arg(i)
                                              .arg(toQString(buffers[i].name))
                                              .arg(buffers[i].size);
    }
}

Error bufferNotFound(const std::string& name, std::string detail) {
    Error error = makeError(ErrorCode::TargetRttBufferNotFound, std::move(detail));
    error.message = QCoreApplication::translate("hwio::jlink", "RTT buffer \"%1\" not found.")
                        .arg(toQString(name))
                        .toStdString();
    return error;
}

} // namespace

std::string speedDetail(std::optional<int> fixedSpeedKhz) {
    return fixedSpeedKhz ? std::format("{} kHz", *fixedSpeedKhz) : std::string("auto speed");
}

std::optional<std::vector<RttBufferInfo>> RttChannel::describe(int count, RttDirection direction,
                                                               Clock::TimePoint deadline,
                                                               const std::stop_token& stopToken) {
    std::vector<RttBufferInfo> infos;
    const int bounded = std::min(count, kMaxRttBuffers);
    const bool startedLate = m_clock.now() >= deadline;
    for (int index = 0; index < bounded; ++index) {
        if (stopToken.stop_requested()) {
            return std::nullopt;
        }
        if (!startedLate && m_clock.now() >= deadline) {
            break;
        }
        auto info = m_api.rttBufferInfo(index, direction);
        if (!info) {
            break;
        }
        infos.push_back(std::move(*info));
    }
    return infos;
}

RttChannel::RttChannel(IJLinkApi& api, Clock& clock, RttTiming timing, std::optional<int> speedKhz)
    : m_api(api), m_clock(clock), m_timing(timing), m_speedKhz(speedKhz) {}

std::string RttChannel::libraryDetail(std::string detail) {
    const std::string libraryText = m_api.takeErrorText();
    if (!libraryText.empty()) {
        detail += "; J-Link: " + libraryText;
    }
    return detail;
}

Result<RttLayout> RttChannel::attach(const RttNames& names, const std::stop_token& stopToken,
                                     std::optional<std::uint32_t> blockAddress) {
    stop();
    m_terminalName = names.terminal;
    AttachState state{.deadline = m_clock.now() + m_timing.attachDeadline,
                      .loggerDeadline = std::nullopt,
                      .sawControlBlock = false};
    if (blockAddress) {
        qCInfo(log::lcJLink).noquote() << QStringLiteral("Starting RTT (control block at 0x%1)")
                                              .arg(*blockAddress, 8, 16, QLatin1Char('0'));
    } else {
        qCInfo(log::lcJLink) << "Starting RTT";
    }
    while (true) {
        if (stopToken.stop_requested()) {
            return cancelAttach();
        }
        const int started = m_api.rttStart(blockAddress);
        if (started < 0) {
            return std::unexpected(
                makeError(ErrorCode::TargetRttNotFound,
                          libraryDetail(std::format("RTT start returned {}", started))));
        }
        m_started = true;

        auto counts = pollCounts(state.deadline, stopToken);
        if (!counts) {
            return std::unexpected(std::move(counts).error());
        }
        if (const std::optional<BufferCounts> found = *counts; !found) {
            // No control block before the deadline.
        } else if (!plausibleCount(found->up) || !plausibleCount(found->down)) {
            qCInfo(log::lcJLink).noquote()
                << QStringLiteral("RTT control block declares %1 up and %2 down buffers (at most "
                                  "%3), retrying search")
                       .arg(found->up)
                       .arg(found->down)
                       .arg(kMaxRttBuffers);
        } else {
            state.sawControlBlock = true;
            auto layout = resolveLayout(names, *found, state, stopToken);
            if (!layout) {
                return std::unexpected(std::move(layout).error());
            }
            if (*layout) {
                return finishAttach(names, **layout, state, stopToken);
            }
        }

        stop();
        if (m_clock.now() >= state.deadline) {
            return std::unexpected(deadlineError(names, state.sawControlBlock));
        }
        if (!m_clock.sleepFor(m_timing.staleRetryDelay, stopToken)) {
            return cancelAttach();
        }
    }
}

Result<RttLayout> RttChannel::cancelAttach() {
    stop();
    return fail(ErrorCode::Cancelled);
}

Result<std::optional<RttChannel::BufferCounts>>
RttChannel::pollCounts(Clock::TimePoint deadline, const std::stop_token& stopToken) {
    while (m_clock.now() < deadline) {
        const int up = m_api.rttBufferCount(RttDirection::Up);
        const int down = m_api.rttBufferCount(RttDirection::Down);
        if (up >= 0 && down >= 0) {
            return BufferCounts{.up = up, .down = down};
        }
        if (!m_clock.sleepFor(m_timing.pollInterval, stopToken)) {
            stop();
            return fail(ErrorCode::Cancelled);
        }
    }
    return std::optional<BufferCounts>{};
}

Result<std::optional<RttLayout>> RttChannel::resolveLayout(const RttNames& names,
                                                           BufferCounts counts, AttachState& state,
                                                           const std::stop_token& stopToken) {
    while (counts.up >= 0 && plausibleCount(counts.up)) {
        const auto described = describe(counts.up, RttDirection::Up, state.deadline, stopToken);
        if (!described) {
            stop();
            return fail(ErrorCode::Cancelled);
        }
        const std::vector<RttBufferInfo>& up = *described;
        const auto terminal = findByName(up, names.terminal);
        if (!terminal) {
            qCInfo(log::lcJLink).noquote() << "RTT buffer" << toQString(names.terminal)
                                           << "not registered yet, retrying search";
            return std::optional<RttLayout>{};
        }
        const std::uint32_t terminalSize = up.at(static_cast<std::size_t>(*terminal)).size;
        if (terminalSize == 0) {
            qCInfo(log::lcJLink) << "RTT control block not initialized yet (terminal buffer size "
                                    "0), retrying search";
            return std::optional<RttLayout>{}; // stale / zeroed control block
        }
        RttLayout layout;
        layout.upCount = counts.up;
        layout.downCount = counts.down;
        layout.terminalUp = RttBuffer{.index = *terminal, .size = terminalSize};
        if (names.logger.empty()) {
            return layout;
        }
        const auto logger = findByName(up, names.logger);
        if (logger && up.at(static_cast<std::size_t>(*logger)).size > 0) {
            layout.loggerUp =
                RttBuffer{.index = *logger, .size = up.at(static_cast<std::size_t>(*logger)).size};
            return layout;
        }
        // Terminal is up, only the logger is missing: give the firmware a short window to
        // register it (descriptors are read live), then go on without logs.
        if (!state.loggerDeadline) {
            state.loggerDeadline = m_clock.now() + m_timing.loggerWait;
        }
        if (m_clock.now() >= *state.loggerDeadline) {
            qCWarning(log::lcJLink).noquote() << "RTT buffer" << toQString(names.logger)
                                              << "not initialized, continuing without logs";
            return layout;
        }
        if (!m_clock.sleepFor(m_timing.pollInterval, stopToken)) {
            stop();
            return fail(ErrorCode::Cancelled);
        }
        counts.up = m_api.rttBufferCount(RttDirection::Up);
    }
    return std::optional<RttLayout>{};
}

Result<RttLayout> RttChannel::finishAttach(const RttNames& names, RttLayout layout,
                                           const AttachState& state,
                                           const std::stop_token& stopToken) {
    const auto describedDown =
        describe(layout.downCount, RttDirection::Down, state.deadline, stopToken);
    const auto describedUp =
        describedDown ? describe(layout.upCount, RttDirection::Up, state.deadline, stopToken)
                      : std::nullopt;
    if (!describedDown || !describedUp) {
        return cancelAttach();
    }
    const std::vector<RttBufferInfo>& down = *describedDown;
    auto downIndex = findByName(down, names.terminal);
    if (!downIndex && std::cmp_less(layout.terminalUp.index, down.size())) {
        downIndex = layout.terminalUp.index; // rttt: the same index as the up buffer
    }
    if (!downIndex) {
        stop();
        return std::unexpected(bufferNotFound(
            names.terminal, std::format("no down buffer \"{}\" among {} down buffers",
                                        names.terminal, layout.downCount)));
    }
    layout.terminalDown =
        RttBuffer{.index = *downIndex, .size = down.at(static_cast<std::size_t>(*downIndex)).size};
    logBuffers(*describedUp, "Up");
    logBuffers(down, "Down");
    qCInfo(log::lcJLink) << "RTT started," << layout.upCount << "up buffers," << layout.downCount
                         << "down buffers";
    m_layout = layout;
    m_attached = true;
    m_api.flushMessages();
    return m_layout;
}

Error RttChannel::deadlineError(const RttNames& names, bool sawControlBlock) {
    if (sawControlBlock) {
        return bufferNotFound(
            names.terminal,
            libraryDetail(std::format("terminal buffer \"{}\" not ready within {} ms",
                                      names.terminal, m_timing.attachDeadline.count())));
    }
    return makeError(
        ErrorCode::TargetRttNotFound,
        libraryDetail(std::format("no RTT control block found within {} ms at {}",
                                  m_timing.attachDeadline.count(), speedDetail(m_speedKhz))));
}

Result<std::size_t> RttChannel::read(int index, std::span<std::byte> buffer) {
    const int result = m_api.rttRead(index, buffer);
    if (result < 0) {
        return std::unexpected(makeError(
            ErrorCode::TargetRttReadFailed,
            libraryDetail(std::format("RTT read of buffer {} returned {}", index, result))));
    }
    return std::min(static_cast<std::size_t>(result), buffer.size());
}

Status RttChannel::write(int index, std::span<const std::byte> data,
                         const std::stop_token& stopToken) {
    std::uint32_t chunkSize = 0;
    if (index == m_layout.terminalDown.index) {
        chunkSize = m_layout.terminalDown.size;
    }
    if (chunkSize == 0) {
        // Not a dropped link: the buffer exists but the firmware never sized it.
        return std::unexpected(
            bufferNotFound(m_terminalName, std::format("down buffer {} has size 0", index)));
    }
    const Clock::TimePoint deadline = m_clock.now() + m_timing.writeTimeout;
    std::size_t total = 0;
    while (total < data.size()) {
        const std::size_t length = std::min<std::size_t>(chunkSize, data.size() - total);
        const int written = m_api.rttWrite(index, data.subspan(total, length));
        if (written < 0) {
            return std::unexpected(makeError(
                ErrorCode::TargetRttWriteFailed,
                libraryDetail(std::format("RTT write to buffer {} returned {}", index, written))));
        }
        if (written == 0) {
            // A target that stops draining the buffer would otherwise spin here forever.
            if (m_clock.now() >= deadline) {
                return std::unexpected(makeError(
                    ErrorCode::TargetRttWriteTimeout,
                    std::format("target is not reading the shell buffer ({} of {} bytes written "
                                "within {} ms)",
                                total, data.size(), m_timing.writeTimeout.count())));
            }
            if (!m_clock.sleepFor(m_timing.writeRetryInterval, stopToken)) {
                return fail(ErrorCode::Cancelled);
            }
            continue;
        }
        total += std::min(static_cast<std::size_t>(written), length);
    }
    return {};
}

void RttChannel::stop() {
    if (m_started) {
        (void)m_api.rttStop();
        m_api.flushMessages();
        m_started = false;
    }
    m_attached = false;
}

} // namespace hwio::jlink
