#include "hio/hw/ble/nus_link.h"

#include "hio/core/error.h"
#include "hio/core/json.h"
#include "hio/core/log.h"
#include "hio/core/result.h"
#include "hio/hw/ble/ble_api.h"
#include "hio/hw/ble/link_handler.h"
#include "hio/hw/ble/types.h"

#include <QLoggingCategory>
#include <QObject>
#include <QTimer>
#include <QtLogging>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace hio::ble {

NusLink::NusLink(IBleApi& api, BleDeviceId device, NusUuids uuids,
                 std::unique_ptr<ILinkHandler> handler, LinkTiming timing)
    : m_api(&api), m_device(std::move(device)), m_uuids(std::move(uuids)),
      m_handler(std::move(handler)), m_timing(timing) {
    m_deadline.setSingleShot(true);
    m_prompt.setSingleShot(true);
    m_write.setSingleShot(true);
    QObject::connect(&m_deadline, &QTimer::timeout, &m_deadline,
                     [this] { connectDeadlinePassed(); });
    QObject::connect(&m_prompt, &QTimer::timeout, &m_prompt, [this] {
        if (m_state == State::Connecting && m_pairingWait) {
            qCInfo(log::lcBle) << "Waiting for pairing (passkey) with" << toQString(m_device);
            m_handler->onPairingPrompt();
        }
    });
    QObject::connect(&m_write, &QTimer::timeout, &m_write, [this] {
        linkFailed(
            makeError(ErrorCode::BleWriteTimeout, std::format("no write confirmation within {} ms",
                                                              m_timing.writeTimeout.count())));
    });
    QObject::connect(&m_flush, &QTimer::timeout, &m_flush, [this] { m_handler->onFlush(); });
    m_flush.start(m_timing.flushInterval);
}

NusLink::~NusLink() {
    stopTimers();
    m_flush.stop();
    if (m_link) {
        m_api->disconnect(*m_link);
        m_link.reset();
    }
    m_handler->onClosed();
}

void NusLink::stopTimers() {
    m_deadline.stop();
    m_prompt.stop();
    m_write.stop();
}

void NusLink::rejectQueued(const Error& error) {
    // Every line not written completely is rejected, also the one in flight (review B10: an
    // explicit reconnect must not drop it silently; the owner counts lines in flight, B4).
    m_current.clear();
    m_offset = 0;
    m_inFlight.clear();
    while (!m_lines.empty()) {
        std::string line = std::move(m_lines.front());
        m_lines.pop_front();
        Error rejected = error;
        rejected.detail = "command not sent: " + error.detail;
        m_handler->onLineRejected(std::move(line), std::move(rejected));
    }
}

void NusLink::disconnect() {
    stopTimers();
    if (m_link) {
        m_api->disconnect(*m_link);
        m_link.reset();
    }
    m_pairingWait = false;
    m_state = State::Down;
    rejectQueued(makeError(ErrorCode::TargetNotConnected, "the link was dropped"));
}

void NusLink::connect(bool automatic) {
    disconnect();
    m_state = State::Connecting;
    m_ready = LinkReady{};
    m_handler->onConnecting(automatic);
    auto link = m_api->connect(m_device, m_uuids);
    if (!link) {
        m_state = State::Down;
        m_handler->onDown(std::move(link).error());
        return;
    }
    m_link = *link;
    m_deadline.start(m_timing.connectDeadline);
}

void NusLink::fail(Error error) {
    disconnect();
    m_handler->onDown(std::move(error));
}

void NusLink::connectDeadlinePassed() {
    if (m_state != State::Connecting) {
        return;
    }
    // Q6: the CCCD write still pending = the passkey was never entered.
    Error error =
        m_pairingWait
            ? makeError(ErrorCode::BleNotPaired, std::format("pairing not completed within {} ms",
                                                             m_timing.pairingDeadline.count()))
            : makeError(ErrorCode::BleConnectFailed, std::format("not connected within {} ms",
                                                                 m_timing.connectDeadline.count()));
    linkFailed(std::move(error));
}

void NusLink::onLinkConnecting(bool waitingForPairing) {
    if (m_state != State::Connecting || !waitingForPairing || m_pairingWait) {
        return;
    }
    m_pairingWait = true;
    m_deadline.start(m_timing.pairingDeadline); // ADR-0084: 60 s while the CCCD write is pending
    m_prompt.start(m_timing.pairingPromptDelay);
}

void NusLink::onLinkReady(LinkReady ready) {
    if (m_state != State::Connecting) {
        return;
    }
    stopTimers();
    m_pairingWait = false;
    ready.mtu = std::max(ready.mtu, defaultMtu);
    m_ready = ready;
    m_state = State::Ready;
    qCInfo(log::lcBle).noquote() << QStringLiteral("Link ready: %1, MTU %2, %3")
                                        .arg(toQString(m_device))
                                        .arg(ready.mtu)
                                        .arg(ready.writeMode == WriteMode::WithoutResponse
                                                 ? QStringLiteral("write without response")
                                                 : QStringLiteral("write with response"));
    m_handler->onReady(ready);
}

void NusLink::onLinkData(std::span<const std::byte> data) {
    if (m_state == State::Ready || m_state == State::Connecting) {
        m_handler->onData(data);
    }
}

void NusLink::onLinkLost(Error error) {
    stopTimers();
    m_link.reset(); // gone already: no disconnect()
    m_pairingWait = false;
    m_state = State::Down;
    const Error lineError = error;
    m_handler->onDown(std::move(error));
    rejectQueued(lineError);
}

void NusLink::linkFailed(Error error) {
    stopTimers();
    if (m_link) {
        m_api->disconnect(*m_link);
        m_link.reset();
    }
    m_pairingWait = false;
    m_state = State::Down;
    qCInfo(log::lcBle).noquote() << "Link down:" << toQString(m_device) << toQString(error.detail);
    const Error lineError = error;
    m_handler->onDown(std::move(error));
    rejectQueued(lineError);
}

void NusLink::sendLine(std::string line) {
    if (m_state != State::Ready) {
        m_handler->onLineRejected(std::move(line),
                                  makeError(ErrorCode::TargetNotConnected, "command not sent"));
        return;
    }
    if (line.size() > maxLineBytes) {
        Error error =
            makeError(ErrorCode::UsageInvalidArgument,
                      std::format("line of {} bytes (at most {})", line.size(), maxLineBytes));
        m_handler->onLineRejected(std::move(line), std::move(error));
        return;
    }
    if (m_lines.size() >= maxQueuedLines) {
        m_handler->onLineRejected(
            std::move(line), makeError(ErrorCode::InternalQueueFull,
                                       std::format("{} lines wait to be written", maxQueuedLines)));
        return;
    }
    m_lines.push_back(std::move(line));
    pump();
}

void NusLink::onWriteDone(std::uint64_t token, std::optional<Error> error) {
    const auto it = std::ranges::find(m_inFlight, token);
    if (m_state != State::Ready || it == m_inFlight.end()) {
        return; // a write of an earlier link
    }
    if (error) {
        linkFailed(std::move(*error));
        return;
    }
    m_inFlight.erase(it);
    m_write.stop();
    pump();
}

void NusLink::pump() {
    const auto payload = static_cast<std::size_t>(m_ready.mtu - attWriteHeader);
    const std::size_t limit =
        m_ready.writeMode == WriteMode::WithoutResponse ? maxChunksInFlight : 1;
    while (m_state == State::Ready && m_link) {
        if (m_current.empty()) {
            if (m_lines.empty()) {
                return;
            }
            m_current = m_lines.front() + "\n";
            m_offset = 0;
        }
        if (m_offset >= m_current.size()) {
            if (!m_inFlight.empty()) {
                break; // the last chunks are not confirmed yet
            }
            std::string line = std::move(m_lines.front());
            m_lines.pop_front();
            m_current.clear();
            m_offset = 0;
            m_handler->onLineWritten(std::move(line)); // only now (ADR-0082)
            continue;
        }
        if (m_inFlight.size() >= limit) {
            break;
        }
        const std::size_t size = std::min(payload, m_current.size() - m_offset);
        const auto chunk = std::as_bytes(std::span(m_current).subspan(m_offset, size));
        const std::uint64_t token = m_nextToken++;
        m_inFlight.push_back(token);
        m_offset += size;
        const LinkId link = *m_link;
        if (Status written = m_api->write(link, chunk, m_ready.writeMode, token); !written) {
            linkFailed(std::move(written).error());
            return;
        }
    }
    if (!m_inFlight.empty() && !m_write.isActive()) {
        m_write.start(m_timing.writeTimeout);
    }
}

void NusLink::pairingFinished(Status status, bool alreadyPaired) {
    m_handler->onPairingFinished(std::move(status), alreadyPaired);
}

} // namespace hio::ble
