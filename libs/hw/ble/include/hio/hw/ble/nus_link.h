#pragma once

// NusLink - one console link over the Nordic UART Service, platform neutral on IBleApi (ADR-0082,
// ADR-0084, ADR-0129; plan/features/0011). Transport only: connect deadline, pairing wait,
// chunking at the MTU, write modes, write timeout. What the bytes mean is the ILinkHandler's
// business (libs/device).
//
//  - connect(): IBleApi::connect; Connecting until TX notifications are enabled (onLinkReady). The
//    attempt must finish within connectDeadline (ble.connect_failed); an attempt that fails with
//    ble.connect_failed before the CCCD write is tried again after connectRetryDelay while the
//    deadline allows it, and only the final failure is reported (HIL H6); after an attempt that
//    suggests BlueZ is still busy with an earlier one (H8) the delay grows (2 s, 4 s, ...); once
//    the CCCD write is pending (an unbonded link pairs) the deadline becomes pairingDeadline from
//    then (ble.not_paired when it passes, Q6) and onPairingPrompt() follows after
//    pairingPromptDelay.
//  - sendLine(): line + "\n" in chunks of MTU - 3 bytes. Write-with-response: one chunk in flight;
//    write-without-response (only when the RX characteristic has that property bit): up to
//    maxChunksInFlight chunks handed to the stack. The handler's onLineWritten (the echo) follows
//    the last chunk. A chunk that does not complete within writeTimeout ends the link
//    (ble.write_timeout); a failed write ends it with the write's error.
//  - At most maxQueuedLines lines wait (internal.queue_full beyond, the line is rejected); a line
//    while not connected is rejected with target.not_connected. Every line ends with exactly one
//    onLineWritten() or onLineRejected(): when the link goes down, the line in flight and the
//    queued ones are rejected with the link's error; when it is dropped (reconnect) with
//    target.not_connected.
//
// Thread safety: BLE thread only (created, used and destroyed there; its QTimers live there).

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/hw/ble/ble_api.h"
#include "hio/hw/ble/link_handler.h"
#include "hio/hw/ble/types.h"

#include <QTimer>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace hio::ble {

struct LinkTiming {
    std::chrono::milliseconds connectDeadline{30000}; // HIL H3: BlueZ needs 6-20 s
    std::chrono::milliseconds pairingDeadline{60000};
    std::chrono::milliseconds pairingPromptDelay{2000};
    std::chrono::milliseconds writeTimeout{2000};
    std::chrono::milliseconds flushInterval{250};
    // HIL H6: a failed connect attempt (ble.connect_failed before the CCCD write, e.g. BlueZ's
    // UnknownError) is tried again after this delay while the connect deadline allows it.
    std::chrono::milliseconds connectRetryDelay{1000};
    // HIL H8: Qt reports BlueZ's D-Bus NoReply (Device1.Connect gave up after ~25 s, BlueZ keeps
    // connecting) and the following InProgress only as UnknownError. An attempt that failed after
    // at least connectBusyAttempt, or within connectImmediateFailure, suggests BlueZ is still busy:
    // the next attempt waits connectBusyRetryDelay, doubled for each further such failure.
    std::chrono::milliseconds connectBusyAttempt{20000};
    std::chrono::milliseconds connectImmediateFailure{1000};
    std::chrono::milliseconds connectBusyRetryDelay{2000};
};

class NusLink {
public:
    static constexpr std::size_t maxLineBytes = 4096; // without the "\n"
    static constexpr std::size_t maxQueuedLines = 256;
    static constexpr std::size_t maxChunksInFlight = 16; // write-without-response

    enum class State : std::uint8_t { Idle, Connecting, Ready, Down };

    NusLink(IBleApi& api, BleDeviceId device, NusUuids uuids, std::unique_ptr<ILinkHandler> handler,
            LinkTiming timing = {});
    // Disconnects, flushes and closes the handler (onClosed()).
    ~NusLink();
    NusLink(const NusLink&) = delete;
    NusLink& operator=(const NusLink&) = delete;
    NusLink(NusLink&&) = delete;
    NusLink& operator=(NusLink&&) = delete;

    // (Re)connects: an existing link is disconnected first; queued lines are rejected.
    void connect(bool automatic);
    // Ends the link without reporting it (the owner asked); the handler stays.
    void disconnect();
    void sendLine(std::string line);
    void pairingFinished(Status status, bool alreadyPaired);
    // Reports `error` as onDown without a connect attempt (e.g. the adapter is off).
    void fail(Error error);
    // fail(), and automatic connect() calls are ignored until a non-automatic one (N4).
    void stop(Error error);

    // IBleApi events of this link (the worker routes them by LinkId).
    [[nodiscard]] std::optional<LinkId> link() const noexcept { return m_link; }
    void onLinkConnecting(bool waitingForPairing);
    void onLinkReady(LinkReady ready);
    void onLinkData(std::span<const std::byte> data);
    void onWriteDone(std::uint64_t token, std::optional<Error> error);
    void onLinkLost(Error error);

    [[nodiscard]] const BleDeviceId& device() const noexcept { return m_device; }
    [[nodiscard]] State state() const noexcept { return m_state; }
    [[nodiscard]] std::uint16_t mtu() const noexcept { return m_ready.mtu; }

private:
    void pump();
    void linkFailed(Error error);
    void rejectQueued(const Error& error);
    void connectDeadlinePassed();
    void startAttempt();
    void attemptFailed(Error error);
    void stopTimers();

    IBleApi* m_api; // non-owning; the worker's, outlives the link
    BleDeviceId m_device;
    NusUuids m_uuids;
    std::unique_ptr<ILinkHandler> m_handler;
    LinkTiming m_timing;

    State m_state = State::Idle;
    std::optional<LinkId> m_link;
    bool m_pairingWait = false;
    bool m_automaticBlocked = false; // stop(): until a non-automatic connect()
    LinkReady m_ready;

    std::deque<std::string> m_lines;      // <= maxQueuedLines; front = being written
    std::string m_current;                // front line + "\n" while it is written
    std::size_t m_offset = 0;             // bytes of m_current handed to write()
    std::deque<std::uint64_t> m_inFlight; // tokens of unfinished writes
    std::uint64_t m_nextToken = 1;

    QTimer m_deadline;                                  // connect / pairing deadline
    QTimer m_prompt;                                    // pairing prompt delay
    QTimer m_write;                                     // write timeout
    QTimer m_flush;                                     // periodic handler flush
    QTimer m_retry;                                     // the next connect attempt (H6)
    std::chrono::steady_clock::time_point m_deadlineAt; // of the running connect (H6)
    std::optional<Error> m_lastAttemptError;            // reported if the deadline ends retrying
    int m_attempts = 0;
    std::chrono::steady_clock::time_point m_attemptStarted; // H8
    int m_busyRetries = 0;                                  // H8: backoff exponent
};

} // namespace hio::ble
