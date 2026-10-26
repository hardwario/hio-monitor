#pragma once

// BleSession - a console session over Bluetooth LE (NUS) (ADR-0079, ADR-0080, ADR-0082, ADR-0083,
// ADR-0084, ADR-0128, ADR-0129; plan/features/0011-m5-ble-backend-cli.md).
//
// A QObject facade on the owner thread. The link lives on the application's BLE thread
// (ble::BleHost): a NusLink (transport) with this session's link handler, which runs the console
// pipeline there - the LineAssembler with the rules of the profile's console target, prompt
// detection (the partial line equals the profile's shellPrompt after ANSI stripping: not emitted
// as a line, promptSeen() instead) and the ConsoleFileSink (flushed every <= 250 ms and on close).
// Finished lines and state events come back through a bounded event queue (eventQueueCapacity;
// when full only the oldest device output lines are dropped, reported as linesDropped).
//
// open() is non-blocking (ADR-0053): it validates, takes the registry slot and the BleLock, posts
// the connect and returns. Connecting until TX notifications are enabled, then Connected (with
// the MTU); a failed first attempt is Stopped (without autoReconnect) or Disconnected (with it);
// adapter, permission, service and pairing errors are always Stopped (plan 0011 Q9). With
// autoReconnect a Disconnected session scans in the background and retries every
// reconnectInterval (counted from the end of the previous attempt) while the device was seen in
// the last seenWindow; reconnect() always tries at once. close() returns at once (plan 0011
// decisions, 2026-10-19): it releases the registry slot and the BleLock, and the BLE thread then
// cancels a running connect, disconnects, closes the console file and destroys the link handler
// (within one BLE-thread turn; BleHost::stop() finishes it in any case). The host runs commands in
// order, so a new session of the same device opened right after close() connects and opens its
// console file only after the old link and file were closed.
//
// Thread safety: every member function must be called on the owner thread.

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/ble_lock.h"
#include "hio/device/console_line.h"
#include "hio/device/console_log.h"
#include "hio/device/console_session.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"
#include "hio/hw/ble/ble_host.h"
#include "hio/hw/ble/nus_link.h"
#include "hio/hw/ble/types.h"

#include <QList>
#include <QObject>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace hio::device {

class SessionRegistry;

namespace detail {
struct BleSessionInternals;
struct BlePromptSeen;
struct BlePairingPrompt;
struct BlePairingFinished;
struct BleConsoleFileChanged;
struct BleAttemptEnded;
struct BleLineRejected;
using BleSessionEvent =
    std::variant<ConsoleLine, ConnectionEvent, Warning, BlePromptSeen, BlePairingPrompt,
                 BlePairingFinished, BleConsoleFileChanged, BleAttemptEnded, BleLineRejected>;
} // namespace detail

struct BleSessionOptions {
    const DeviceProfile* profile = nullptr; // must have profile->ble
    ble::BleDeviceId deviceId;
    std::string deviceName; // for display and logs; may be empty
    bool autoReconnect = false;
    // nullopt = default console file, empty path = none, otherwise this file (M1 rules).
    std::optional<std::filesystem::path> consoleFile;
    // ADR-0063: nullptr = the session creates its own log.
    std::shared_ptr<ConsoleLog> consoleLog;
    // ADR-0156 (M6): the default console file goes into this directory (same generated name);
    // an explicit consoleFile (or an empty path = none) wins. nullopt = the default directory.
    std::optional<std::filesystem::path> consoleFileDir = std::nullopt;
};

class BleSession final : public ConsoleSession {
    Q_OBJECT

    struct Passkey {
        explicit Passkey() = default;
    };

public:
    static constexpr std::size_t eventQueueCapacity = 10000;
    // Lines sent and not yet echoed or rejected (synchronous back-pressure, review B4).
    static constexpr std::size_t maxLinesInFlight = ble::NusLink::maxQueuedLines;
    static constexpr std::chrono::milliseconds reconnectInterval{3000};
    static constexpr std::chrono::milliseconds seenWindow{15000};

    // Errors: usage.invalid_argument (no profile, a profile without BLE, no device id),
    // internal.session_limit, ble.in_use_by_other_process (this or another process),
    // io.write_failed (lock directory), internal.queue_full / cancelled (the host).
    [[nodiscard]] static Result<std::unique_ptr<BleSession>>
    open(SessionRegistry& registry, ble::BleHost& host, BleSessionOptions options);

    BleSession(Passkey key, SessionRegistry& registry, ble::BleHost& host,
               std::unique_ptr<detail::BleSessionInternals> internals);
    ~BleSession() override; // closes
    BleSession(const BleSession&) = delete;
    BleSession& operator=(const BleSession&) = delete;
    BleSession(BleSession&&) = delete;
    BleSession& operator=(BleSession&&) = delete;

    // Errors: usage.invalid_argument (a line longer than ble::NusLink::maxLineBytes),
    // internal.queue_full (maxLinesInFlight lines are not written or rejected yet, or the host's
    // command queue is full: send it again later, review B4), cancelled (closed). A line while not
    // connected is a warning (target.not_connected).
    [[nodiscard]] Status sendLine(std::string line) override;
    [[nodiscard]] Status reconnect() override;
    // App-initiated pairing (ADR-0084); the result arrives as pairingFinished() (and a warning
    // when it failed). Errors: internal.queue_full, cancelled.
    [[nodiscard]] Status pair();

    [[nodiscard]] const ConsoleLog& log() const override;
    [[nodiscard]] const ConnectionEvent& connection() const noexcept override {
        return m_connection;
    }
    // {Ble, device id, device name (the id when the name is empty)}.
    [[nodiscard]] SessionIdentity identity() const override;
    // None: no reset, no separate log channel, no operations (ADR-0081).
    [[nodiscard]] Capabilities capabilities() const override;
    [[nodiscard]] const DeviceProfile& profile() const noexcept override;
    [[nodiscard]] std::string_view consoleTargetId() const override;
    [[nodiscard]] std::filesystem::path consoleFile() const override;
    // The last RSSI a background scan saw (nullopt before any).
    [[nodiscard]] std::optional<int> rssi() const;
    // The negotiated MTU, 0 until Connected.
    [[nodiscard]] std::uint16_t mtu() const;

signals:
    // The CCCD write has been pending for 2 s: "enter the passkey in the system dialog".
    void pairingPrompt();
    void pairingFinished(const hio::Status& status);

protected:
    void closeSession() override;

private:
    friend struct detail::BleSessionInternals;

    Q_INVOKABLE void drainEvents();
    void wakeUp(); // BLE thread: one coalesced queued drainEvents()
    void drainScan();
    [[nodiscard]] bool handleEvent(detail::BleSessionEvent& event, QList<ConsoleLine>& batch);
    void connectionUpdated();
    void startBackgroundScan();
    void stopBackgroundScan();
    void scanFailed(const Error& error);
    void retryTick();
    void closeNow();

    SessionRegistry* m_registry;
    ble::BleHost* m_host;
    std::unique_ptr<detail::BleSessionInternals> m_internals;
    std::atomic<bool> m_wakePending{false};
    ConnectionEvent m_connection;
    std::filesystem::path m_consoleFile;
    std::uint64_t m_reportedDrops = 0;
    std::uint16_t m_mtu = 0;
    std::optional<int> m_rssi;
    std::optional<std::chrono::steady_clock::time_point> m_lastSeen;
    bool m_scanning = false; // a background scan was requested (autoReconnect)
    std::chrono::steady_clock::time_point m_nextScan; // background scan restarts (B2)
    std::optional<ErrorCode> m_scanErrorShown;        // reported once per code (B2)
    // The origin tokens (ConsoleLine::origin, plan 0021) of the lines not written or rejected yet,
    // in order: each gets exactly one echo or rejection. <= maxLinesInFlight (B4).
    std::deque<std::uint64_t> m_linesInFlight;
    bool m_attemptPending = false; // an automatic attempt was posted and has not ended
    std::chrono::steady_clock::time_point m_nextRetry;
    QTimer m_retryTimer;
    bool m_closed = false;
};

} // namespace hio::device
