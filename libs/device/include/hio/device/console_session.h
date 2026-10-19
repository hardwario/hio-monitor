#pragma once

// ConsoleSession - the console part shared by the J-Link DeviceSession and the BLE BleSession
// (ADR-0079, plan/features/0011-m5-ble-backend-cli.md). Consumers of a console (the CLI console
// and command, the GUI console page, later MCP) only use this interface.
//
// A QObject facade on the owner thread; each implementation has its own transport-specific worker
// (the J-Link session worker thread, ADR-0031; the shared BLE thread, ADR-0080). The signals are
// emitted on the owner thread in the order the worker produced the events.
//
// Thread safety: every member function must be called on the owner thread.

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/console_line.h"
#include "hio/device/console_log.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"

#include <QFlags>
#include <QList>
#include <QObject>
#include <QtTypes>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>
#include <string_view>

namespace hio::device {

// Who the session talks to (ADR-0079).
struct SessionIdentity {
    Transport transport = Transport::JLink;
    std::string id;          // J-Link: the serial number as decimal; BLE: the BleDeviceId
    std::string displayName; // "J-Link 801000001", "CHESTER-ABC123"
};

// What a transport can do besides the console, so the UI can hide the rest (ADR-0079).
enum class Capability : std::uint8_t {
    Reset = 1,      // reset the target from the console
    LogChannel = 2, // a separate device log channel
    Operations = 4, // flash, erase, recover, diagnose, ...
};
Q_DECLARE_FLAGS(Capabilities, Capability)

class ConsoleSession : public QObject {
    Q_OBJECT

public:
    ConsoleSession(const ConsoleSession&) = delete;
    ConsoleSession& operator=(const ConsoleSession&) = delete;
    ConsoleSession(ConsoleSession&&) = delete;
    ConsoleSession& operator=(ConsoleSession&&) = delete;
    ~ConsoleSession() override = default;

    // Queued for the worker; an Error only when the queue is full (internal.queue_full) or the
    // session is closed (cancelled). Results arrive as events (echo line, warning, connection).
    [[nodiscard]] virtual Status sendLine(std::string line) = 0;
    [[nodiscard]] virtual Status reconnect() = 0;
    // Stops the session and releases its registry slot and lock. Idempotent: calls closeSession()
    // once.
    void close();

    // Owner thread only (ADR-0063): the provided or the session's own log.
    [[nodiscard]] virtual const ConsoleLog& log() const = 0;
    // The last reported connection event (Connecting before the first one).
    [[nodiscard]] virtual const ConnectionEvent& connection() const = 0;
    [[nodiscard]] virtual SessionIdentity identity() const = 0;
    [[nodiscard]] virtual Capabilities capabilities() const = 0;
    [[nodiscard]] virtual const DeviceProfile& profile() const = 0;
    // The profile target whose console rules, history and file names the session uses ("app").
    [[nodiscard]] virtual std::string_view consoleTargetId() const = 0;
    // The console log file the session writes now; empty for none (plan 0005 rules).
    [[nodiscard]] virtual std::filesystem::path consoleFile() const = 0;

signals:
    void linesReceived(const QList<hio::device::ConsoleLine>& lines);
    void connectionChanged(const hio::device::ConnectionEvent& event);
    void linesDropped(quint64 count);
    void warning(const hio::Error& error);
    // BLE only (ADR-0082): the shell prompt was seen after output; never emitted for J-Link.
    void promptSeen();

protected:
    ConsoleSession() = default;

    // The concrete close. Each final class's destructor calls its own close path directly (no
    // virtual call from a destructor).
    virtual void closeSession() = 0;

private:
    bool m_closeCalled = false;
};

// CLI helper (ADR-0053): runs a local QEventLoop until the first Connected (success), a
// Disconnected or Stopped event with an error (that error), a Stopped event without an error
// (cancelled: the session was stopped on purpose and does not connect by itself), `timeout`
// (timeout) or a stop request (cancelled).
[[nodiscard]] Status waitUntilConnected(ConsoleSession& session, std::chrono::milliseconds timeout,
                                        const std::stop_token& stop = {});

// The console with --auto-reconnect (rttt behaviour, decided 2026-10-09): waits only for the
// outcome of the first attempt. Success when it Connected or ended Disconnected (the session keeps
// retrying); a Stopped event ends it with its error (or cancelled without one); also timeout and
// cancelled (stop request) as in waitUntilConnected().
[[nodiscard]] Status waitForFirstAttempt(ConsoleSession& session, std::chrono::milliseconds timeout,
                                         const std::stop_token& stop = {});

} // namespace hio::device

Q_DECLARE_OPERATORS_FOR_FLAGS(hio::device::Capabilities)
