#pragma once

// Internals of BleSession shared by the facade, the link handler (BLE thread) and the unit tests
// (plan/features/0011, ADR-0128, ADR-0129).

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/device/ble_lock.h"
#include "hio/device/ble_session.h"
#include "hio/device/console_file_sink.h"
#include "hio/device/console_line.h"
#include "hio/device/console_log.h"
#include "hio/device/line_assembler.h"
#include "hio/device/profile.h"
#include "hio/device/session_events.h"
#include "hio/hw/ble/ble_host.h"
#include "hio/hw/ble/event_channel.h"
#include "hio/hw/ble/link_handler.h"
#include "hio/hw/ble/nus_link.h"
#include "hio/hw/ble/types.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>

namespace hio::device {

class SessionRegistry;

namespace detail {

// ---- events (BLE thread -> owner thread) -------------------------------------------------------

struct BlePromptSeen {};
struct BlePairingPrompt {};
struct BlePairingFinished {
    Status status;
    bool alreadyPaired = false;
};
// The console file the handler writes now (empty: none or stopped after a write failure).
struct BleConsoleFileChanged {
    std::filesystem::path file;
};
// A connect attempt ended (connected or failed), reported even when the state did not change
// (retry pacing of the facade).
struct BleAttemptEnded {};
// A line sent with sendLine() was rejected (its warning follows); with the echo it ends the line's
// in-flight accounting of the facade (review B4).
struct BleLineRejected {};

using BleSessionEvent =
    std::variant<ConsoleLine, ConnectionEvent, Warning, BlePromptSeen, BlePairingPrompt,
                 BlePairingFinished, BleConsoleFileChanged, BleAttemptEnded, BleLineRejected>;

// When the queue is full only device output lines are dropped (as DeviceSession, review R10).
[[nodiscard]] inline bool isDroppableBleEvent(const BleSessionEvent& event) {
    const auto* line = std::get_if<ConsoleLine>(&event);
    return line != nullptr && line->dir == Direction::Out;
}

using BleEventChannel = ble::EventChannel<BleSessionEvent>;

// ---- the link handler (BLE thread, ADR-0128) ----------------------------------------------------

struct BleHandlerConfig {
    const DeviceProfile* profile = nullptr;
    const TargetProfile* target = nullptr; // the BLE console target (routing rules)
    ble::BleDeviceId deviceId;
    std::string deviceName;
    bool autoReconnect = false;
    std::optional<ConsoleFileConfig> consoleFile; // nullopt = no console file
};

class BleConsoleHandler final : public ble::ILinkHandler {
public:
    BleConsoleHandler(BleHandlerConfig config, std::shared_ptr<BleEventChannel> channel);
    ~BleConsoleHandler() override;
    BleConsoleHandler(const BleConsoleHandler&) = delete;
    BleConsoleHandler& operator=(const BleConsoleHandler&) = delete;
    BleConsoleHandler(BleConsoleHandler&&) = delete;
    BleConsoleHandler& operator=(BleConsoleHandler&&) = delete;

    void onConnecting(bool automatic) override;
    void onPairingPrompt() override;
    void onReady(ble::LinkReady ready) override;
    void onData(std::span<const std::byte> data) override;
    void onLineWritten(std::string line) override;
    void onLineRejected(std::string line, Error error) override;
    void onDown(Error error) override;
    void onPairingFinished(Status status, bool alreadyPaired) override;
    void onFlush() override;
    void onClosed() override;

private:
    void emitEvent(BleSessionEvent event);
    void emitLine(ConsoleLine line);
    void warn(Error error);
    void report(ConnectionState state, std::optional<Error> error = std::nullopt,
                bool force = false);
    void openSink();
    void sinkResult(const Status& status);
    void reportConsoleFile();

    BleHandlerConfig m_config;
    std::shared_ptr<BleEventChannel> m_channel;
    LineAssembler m_shell;
    std::string m_prompt;
    bool m_afterPrompt = false; // the next shell line ends the (not emitted) prompt line (H5)
    bool m_afterWrite = false;  // the next shell line is the answer to a written line (H5)
    std::unique_ptr<ConsoleFileSink> m_sink;
    bool m_sinkOpened = false;
    std::filesystem::path m_reportedConsoleFile;
    std::optional<ConnectionState> m_reported;
    std::optional<ErrorCode> m_reportedCode;
    std::uint16_t m_mtu = 0;
    int m_attempts = 0;
    bool m_everConnected = false;
    bool m_fatal = false;
    bool m_closed = false;
};

// Errors that end a BLE session for good, also with autoReconnect (plan 0011 Q9).
[[nodiscard]] bool isFatalBleError(ErrorCode code) noexcept;

// True when `partial` (raw bytes of the incomplete line) is the prompt: ANSI CSI sequences and
// carriage-return redraws removed, it equals `prompt` (ADR-0082).
[[nodiscard]] bool isPrompt(std::string_view partial, std::string_view prompt);

// ---- facade state
// ---------------------------------------------------------------------------------

struct BleSessionTiming {
    ble::LinkTiming link;
    std::chrono::milliseconds reconnectInterval = BleSession::reconnectInterval;
    std::chrono::milliseconds seenWindow = BleSession::seenWindow;
};

struct BleSessionInternals {
    const DeviceProfile* profile = nullptr;
    const TargetProfile* target = nullptr;
    ble::BleDeviceId deviceId;
    std::string deviceName;
    bool autoReconnect = false;
    std::shared_ptr<ConsoleLog> log;
    std::optional<BleLock> lock;
    ble::ClientId client = 0;
    BleSessionTiming timing;
    std::shared_ptr<BleEventChannel> events;
    std::shared_ptr<ble::Subscription> scan; // background scan (autoReconnect)

    [[nodiscard]] static Result<std::unique_ptr<BleSession>> open(SessionRegistry& registry,
                                                                  ble::BleHost& host,
                                                                  BleSessionOptions options,
                                                                  BleSessionTiming timing);
};

// BleSession::open() with test timings.
[[nodiscard]] Result<std::unique_ptr<BleSession>> openBleSession(SessionRegistry& registry,
                                                                 ble::BleHost& host,
                                                                 BleSessionOptions options,
                                                                 BleSessionTiming timing);

} // namespace detail

} // namespace hio::device
